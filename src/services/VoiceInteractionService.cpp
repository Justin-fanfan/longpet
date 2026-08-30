#include "VoiceInteractionService.h"

#include "services/MediaSessionCoordinator.h"
#include "services/VoiceInteractionPorts.h"

#include <QDebug>

#include <utility>

namespace {
constexpr int MaximumPendingTtsSentences = 8;

VoiceInteractionState terminalStateFor(const AiProviderError& error)
{
    return error.code == AiProviderErrorCode::NetworkError
        || error.code == AiProviderErrorCode::Timeout
        ? VoiceInteractionState::Offline : VoiceInteractionState::Error;
}
}

const QString VoiceInteractionService::MediaOwner = QStringLiteral("voice_interaction");

VoiceInteractionService::VoiceInteractionService(
    const AiConfiguration& configuration,
    AsrProviderPort* asrProvider,
    LlmProviderPort* llmProvider,
    TtsProviderPort* ttsProvider,
    VoiceAudioPort* audio,
    MediaSessionCoordinator* mediaSessions,
    QObject* parent)
    : QObject(parent),
      m_configuration(configuration),
      m_asrProvider(asrProvider),
      m_llmProvider(llmProvider),
      m_ttsProvider(ttsProvider),
      m_audio(audio),
      m_mediaSessions(mediaSessions),
      m_sentenceBuffer(configuration.voice.sentenceMinimumCharacters,
                       configuration.voice.sentenceMaximumCharacters),
      m_vad(configuration.voice.vadThresholdDb,
            configuration.voice.vadSilenceTimeoutMs,
            configuration.voice.recordingMinimumMs,
            configuration.voice.vadMinimumSpeechMs)
{
    m_recordingDeadline.setSingleShot(true);
    m_recordingDeadline.setInterval(qMax(1'000, configuration.voice.recordingMaximumMs));
    connect(&m_recordingDeadline, &QTimer::timeout, this, [this] {
        if (m_snapshot.state == VoiceInteractionState::Listening) {
            qInfo().noquote() << QStringLiteral(
                "Voice interaction session=%1 event=recording_maximum_reached")
                    .arg(m_snapshot.sessionId);
            finishRecording();
        }
    });

    m_uiUpdateTimer.setSingleShot(true);
    m_uiUpdateTimer.setInterval(40);
    connect(&m_uiUpdateTimer, &QTimer::timeout, this, [this] {
        if (m_snapshot.isActive())
            emit snapshotChanged(m_snapshot);
    });

    if (m_audio) {
        connect(m_audio, &VoiceAudioPort::recordingStarted,
                this, &VoiceInteractionService::handleRecordingStarted);
        connect(m_audio, &VoiceAudioPort::recordingProgress,
                this, &VoiceInteractionService::handleRecordingProgress);
        connect(m_audio, &VoiceAudioPort::recordingReady,
                this, &VoiceInteractionService::handleRecordingReady);
        connect(m_audio, &VoiceAudioPort::playbackStarted,
                this, &VoiceInteractionService::handlePlaybackStarted);
        connect(m_audio, &VoiceAudioPort::playbackFinished,
                this, &VoiceInteractionService::handlePlaybackFinished);
        connect(m_audio, &VoiceAudioPort::cancellationFinished,
                this, &VoiceInteractionService::handleAudioCancellationFinished);
        connect(m_audio, &VoiceAudioPort::audioFailed,
                this, &VoiceInteractionService::handleAudioFailure);
    }
    if (m_asrProvider) {
        connect(m_asrProvider, &AsrProviderPort::transcriptionReady,
                this, &VoiceInteractionService::handleTranscription);
        connect(m_asrProvider, &AsrProviderPort::requestFailed,
                this, &VoiceInteractionService::handleAsrFailure);
    }
    if (m_llmProvider) {
        connect(m_llmProvider, &LlmProviderPort::chatDelta,
                this, &VoiceInteractionService::handleLlmDelta);
        connect(m_llmProvider, &LlmProviderPort::chatCompletionReady,
                this, &VoiceInteractionService::handleChatCompletion);
        connect(m_llmProvider, &LlmProviderPort::requestFailed,
                this, &VoiceInteractionService::handleLlmFailure);
    }
    if (m_ttsProvider) {
        connect(m_ttsProvider, &TtsProviderPort::speechReady,
                this, &VoiceInteractionService::handleSpeech);
        connect(m_ttsProvider, &TtsProviderPort::requestFailed,
                this, &VoiceInteractionService::handleTtsFailure);
    }
}

VoiceInteractionService::~VoiceInteractionService()
{
    if (m_asrProvider)
        m_asrProvider->cancel(m_snapshot.sessionId);
    if (m_llmProvider)
        m_llmProvider->cancel(m_snapshot.sessionId);
    if (m_ttsProvider)
        m_ttsProvider->cancel(m_snapshot.sessionId);
    if (m_audio)
        m_audio->cancel(m_snapshot.sessionId);
    releaseResources();
}

VoiceInteractionSnapshot VoiceInteractionService::snapshot() const
{
    return m_snapshot;
}

VoiceInteractionResult VoiceInteractionService::startInteraction()
{
    if (m_snapshot.isActive() || m_restartPending)
        return {false, QStringLiteral("语音交互正在进行"), m_snapshot};
    if (m_audioCancellationSessionId != 0) {
        m_restartPending = true;
        m_restartWaitingSessionId = m_audioCancellationSessionId;
        m_snapshot.statusMessage = QStringLiteral("正在准备麦克风");
        emit snapshotChanged(m_snapshot);
        return {true, {}, m_snapshot};
    }

    return beginInteraction();
}

VoiceInteractionResult VoiceInteractionService::restartInteraction()
{
    if (!m_snapshot.isActive() && !m_restartPending)
        return startInteraction();
    if (m_restartPending)
        return {true, {}, m_snapshot};
    cancelSession(true);
    return {true, {}, m_snapshot};
}

VoiceInteractionResult VoiceInteractionService::beginInteraction()
{
    m_snapshot = {};
    m_snapshot.sessionId = ++m_nextSessionId;
    resetSessionWork();
    m_sessionClock.start();
    const QString configurationError = m_configuration.validationError();
    if (!configurationError.isEmpty())
        return fail(QStringLiteral("Config"), configurationError);
    if (!m_asrProvider || !m_llmProvider || !m_ttsProvider || !m_audio)
        return fail(QStringLiteral("Config"), QStringLiteral("语音服务尚未准备好"));
    if (m_mediaSessions && !m_mediaSessions->tryAcquire(MediaOwner))
        return fail(QStringLiteral("Media"), QStringLiteral("设备正在通话，请稍后再试"));

    m_resourcesActive = true;
    emit activityChanged(true);
    qInfo().noquote() << QStringLiteral(
        "Voice interaction session=%1 event=start vad=%2 llm_stream=%3 sentence_tts=%4")
            .arg(m_snapshot.sessionId)
            .arg(m_configuration.voice.vadEnabled)
            .arg(m_configuration.voice.llmStreamEnabled)
            .arg(m_configuration.voice.sentenceTtsEnabled);
    publish(VoiceInteractionState::Listening, QStringLiteral("正在打开麦克风"));
    m_recordingDeadline.start();
    m_audio->startRecording(m_snapshot.sessionId);
    return {true, {}, m_snapshot};
}

VoiceInteractionResult VoiceInteractionService::finishRecording()
{
    if (m_snapshot.state != VoiceInteractionState::Listening) {
        return {false, QStringLiteral("当前没有正在进行的录音"), m_snapshot};
    }
    m_recordingDeadline.stop();
    const qint64 now = elapsedMs();
    m_metrics.recordingEndedAt = now;
    m_metrics.recordingDurationMs = m_lastCapturedMs > 0
        ? m_lastCapturedMs
        : qMax<qint64>(0, now - m_metrics.recordingStartedAt);
    publish(VoiceInteractionState::Recognizing, QStringLiteral("正在整理录音"));
    m_audio->finishRecording(m_snapshot.sessionId);
    return {true, {}, m_snapshot};
}

VoiceInteractionResult VoiceInteractionService::cancelInteraction()
{
    if (m_restartPending) {
        m_restartPending = false;
        m_restartWaitingSessionId = 0;
    }
    if (!m_snapshot.isActive())
        return {true, {}, m_snapshot};
    cancelSession(false);
    return {true, {}, m_snapshot};
}

bool VoiceInteractionService::clearConversationHistory()
{
    if (m_snapshot.isActive() || m_restartPending)
        return false;
    m_history.clear();
    qInfo("Voice interaction history cleared");
    return true;
}

bool VoiceInteractionService::acceptsSession(quint64 sessionId) const
{
    return sessionId == m_snapshot.sessionId && m_snapshot.isActive();
}

void VoiceInteractionService::setWeatherProvider(
    std::function<std::optional<WeatherSnapshot>()> provider)
{
    m_weatherProvider = std::move(provider);
}

void VoiceInteractionService::handleRecordingStarted(quint64 sessionId)
{
    if (sessionId != m_snapshot.sessionId
        || m_snapshot.state != VoiceInteractionState::Listening) {
        return;
    }
    m_metrics.recordingStartedAt = elapsedMs();
    m_snapshot.statusMessage = QStringLiteral("正在聆听，请说话");
    emit snapshotChanged(m_snapshot);
}

void VoiceInteractionService::handleRecordingProgress(
    quint64 sessionId, qint64 capturedMs, double levelDb)
{
    if (sessionId != m_snapshot.sessionId
        || m_snapshot.state != VoiceInteractionState::Listening) {
        return;
    }
    m_lastCapturedMs = qMax(m_lastCapturedMs, capturedMs);
    if (!m_configuration.voice.vadEnabled)
        return;

    const VoiceActivityUpdate update = m_vad.process(capturedMs, levelDb);
    if (update.speechDetected) {
        m_snapshot.speechDetected = true;
        m_snapshot.statusMessage = QStringLiteral("听到您说话了");
        qInfo().noquote() << QStringLiteral(
            "Voice interaction session=%1 event=vad_speech_detected captured_ms=%2 level_db=%3")
                .arg(sessionId).arg(capturedMs).arg(levelDb, 0, 'f', 1);
        emit snapshotChanged(m_snapshot);
    }
    if (update.shouldStop) {
        qInfo().noquote() << QStringLiteral(
            "Voice interaction session=%1 event=vad_end_of_speech captured_ms=%2")
                .arg(sessionId).arg(capturedMs);
        finishRecording();
    }
}

void VoiceInteractionService::handleRecordingReady(
    quint64 sessionId, const QByteArray& wavAudio)
{
    if (sessionId != m_snapshot.sessionId
        || m_snapshot.state != VoiceInteractionState::Recognizing) {
        return;
    }
    if (wavAudio.size() <= 44) {
        fail(QStringLiteral("Recording"),
             QStringLiteral("没有录到有效声音，请再试一次"),
             QStringLiteral("recording returned %1 bytes").arg(wavAudio.size()));
        return;
    }
    m_metrics.asrStartedAt = elapsedMs();
    m_snapshot.statusMessage = QStringLiteral("正在识别");
    emit snapshotChanged(m_snapshot);
    qInfo().noquote() << QStringLiteral(
        "Voice interaction session=%1 stage=ASR event=request_start recording_ms=%2 bytes=%3")
            .arg(sessionId).arg(m_metrics.recordingDurationMs).arg(wavAudio.size());
    m_asrProvider->transcribe(sessionId, wavAudio);
}

void VoiceInteractionService::handleTranscription(quint64 sessionId,
                                                   const QString& text)
{
    if (sessionId != m_snapshot.sessionId
        || m_snapshot.state != VoiceInteractionState::Recognizing) {
        return;
    }
    m_metrics.asrDurationMs = elapsedMs() - m_metrics.asrStartedAt;
    const QString transcript = text.trimmed();
    if (transcript.isEmpty()) {
        fail(QStringLiteral("ASR"), QStringLiteral("没有听清，请再说一次"),
             QStringLiteral("ASR returned empty text"));
        return;
    }
    qInfo().noquote() << QStringLiteral(
        "Voice interaction session=%1 stage=ASR event=completed duration_ms=%2 text_chars=%3")
            .arg(sessionId).arg(m_metrics.asrDurationMs).arg(transcript.size());
    m_snapshot.transcript = transcript;
    m_snapshot.generationActive = true;
    publish(VoiceInteractionState::Thinking, QStringLiteral("正在思考"));
    m_metrics.llmStartedAt = elapsedMs();
    const QList<AiChatMessage> messages = messagesFor(transcript);
    if (m_configuration.voice.llmStreamEnabled)
        m_llmProvider->streamChat(sessionId, messages);
    else
        m_llmProvider->completeChat(sessionId, messages);
}

void VoiceInteractionService::handleLlmDelta(quint64 sessionId,
                                              const QString& delta)
{
    if (!acceptsSession(sessionId) || !m_snapshot.generationActive
        || delta.isEmpty()) {
        return;
    }
    const qint64 now = elapsedMs();
    if (m_metrics.llmFirstTokenAt < 0) {
        m_metrics.llmFirstTokenAt = now;
        qInfo().noquote() << QStringLiteral(
            "Voice interaction session=%1 stage=LLM event=first_delta ttft_ms=%2")
                .arg(sessionId).arg(now - m_metrics.llmStartedAt);
    }
    m_snapshot.response.append(delta);
    if (m_configuration.voice.sentenceTtsEnabled)
        enqueueSentences(m_sentenceBuffer.append(delta));
    if (!m_uiUpdateTimer.isActive())
        m_uiUpdateTimer.start();
    pumpTts();
}

void VoiceInteractionService::handleChatCompletion(quint64 sessionId,
                                                    const QString& text)
{
    if (!acceptsSession(sessionId) || !m_snapshot.generationActive)
        return;
    const QString response = text.trimmed();
    if (response.isEmpty()) {
        fail(QStringLiteral("LLM"),
             QStringLiteral("暂时没有得到回答，请稍后再试"),
             QStringLiteral("LLM returned empty content"));
        return;
    }

    const qint64 now = elapsedMs();
    if (m_metrics.llmFirstTokenAt < 0)
        m_metrics.llmFirstTokenAt = now;
    if (m_snapshot.response.isEmpty()) {
        m_snapshot.response = response;
        if (m_configuration.voice.sentenceTtsEnabled)
            enqueueSentences(m_sentenceBuffer.append(response));
    } else if (response.startsWith(m_snapshot.response)) {
        const QString remainder = response.mid(m_snapshot.response.size());
        m_snapshot.response = response;
        if (m_configuration.voice.sentenceTtsEnabled && !remainder.isEmpty())
            enqueueSentences(m_sentenceBuffer.append(remainder));
    } else {
        qWarning().noquote() << QStringLiteral(
            "Voice interaction session=%1 stage=LLM event=final_text_mismatch streamed_chars=%2 final_chars=%3")
                .arg(sessionId).arg(m_snapshot.response.size()).arg(response.size());
        m_snapshot.response = response;
    }

    m_snapshot.generationActive = false;
    m_llmFinished = true;
    m_metrics.llmFinishedAt = now;
    m_uiUpdateTimer.stop();
    if (m_configuration.voice.sentenceTtsEnabled) {
        enqueueSentences(m_sentenceBuffer.flush());
    } else {
        m_sentenceBuffer.clear();
        enqueueSentences({response});
    }
    qInfo().noquote() << QStringLiteral(
        "Voice interaction session=%1 stage=LLM event=completed total_ms=%2 response_chars=%3")
            .arg(sessionId).arg(now - m_metrics.llmStartedAt).arg(response.size());
    emit snapshotChanged(m_snapshot);
    pumpTts();
    maybeCompleteInteraction();
}

void VoiceInteractionService::enqueueSentences(const QStringList& sentences)
{
    for (const QString& rawSentence : sentences) {
        const QString sentence = rawSentence.trimmed();
        if (sentence.isEmpty())
            continue;
        if (m_metrics.firstSentenceAt < 0) {
            m_metrics.firstSentenceAt = elapsedMs();
            qInfo().noquote() << QStringLiteral(
                "Voice interaction session=%1 event=first_sentence elapsed_ms=%2 llm_ms=%3")
                    .arg(m_snapshot.sessionId)
                    .arg(m_metrics.firstSentenceAt)
                    .arg(m_metrics.firstSentenceAt - m_metrics.llmStartedAt);
        }
        if (m_ttsTextQueue.size() >= MaximumPendingTtsSentences) {
            QString tail = m_ttsTextQueue.takeLast();
            tail.append(sentence);
            m_ttsTextQueue.enqueue(tail);
        } else {
            m_ttsTextQueue.enqueue(sentence);
        }
    }
}

void VoiceInteractionService::pumpTts()
{
    if (!m_snapshot.isActive() || m_ttsInFlight || m_ttsTextQueue.isEmpty())
        return;
    if (m_playbackQueue.size()
        >= m_configuration.voice.ttsPrebufferSegments) {
        return;
    }

    m_currentTtsText = m_ttsTextQueue.dequeue();
    m_ttsInFlight = true;
    m_currentTtsStartedAt = elapsedMs();
    if (m_metrics.firstTtsStartedAt < 0)
        m_metrics.firstTtsStartedAt = m_currentTtsStartedAt;
    m_snapshot.statusMessage = m_audioPlaying
        ? QStringLiteral("正在回答") : QStringLiteral("正在生成语音");
    emit snapshotChanged(m_snapshot);
    qInfo().noquote() << QStringLiteral(
        "Voice interaction session=%1 stage=TTS event=request_start chars=%2 pending=%3")
            .arg(m_snapshot.sessionId).arg(m_currentTtsText.size())
            .arg(m_ttsTextQueue.size());
    m_ttsProvider->synthesize(m_snapshot.sessionId, m_currentTtsText);
}

void VoiceInteractionService::handleSpeech(quint64 sessionId,
                                           const QByteArray& audio)
{
    if (!acceptsSession(sessionId) || !m_ttsInFlight)
        return;
    const qint64 duration = elapsedMs() - m_currentTtsStartedAt;
    if (m_metrics.firstTtsDurationMs < 0)
        m_metrics.firstTtsDurationMs = duration;
    m_ttsInFlight = false;
    m_currentTtsText.clear();
    m_currentTtsStartedAt = -1;
    if (audio.isEmpty()) {
        ++m_ttsFailureCount;
        qWarning().noquote() << QStringLiteral(
            "Voice interaction session=%1 stage=TTS error=empty_audio")
                .arg(sessionId);
    } else {
        ++m_ttsSuccessCount;
        m_playbackQueue.enqueue(audio);
        qInfo().noquote() << QStringLiteral(
            "Voice interaction session=%1 stage=TTS event=completed duration_ms=%2 bytes=%3")
                .arg(sessionId).arg(duration).arg(audio.size());
    }
    pumpPlayback();
    pumpTts();
    maybeCompleteInteraction();
}

void VoiceInteractionService::pumpPlayback()
{
    if (!m_snapshot.isActive() || m_audioPlaying || m_playbackQueue.isEmpty())
        return;
    m_audioPlaying = true;
    m_snapshot.playbackActive = true;
    m_audio->play(m_snapshot.sessionId, m_playbackQueue.dequeue());
}

void VoiceInteractionService::handlePlaybackStarted(quint64 sessionId)
{
    if (!acceptsSession(sessionId) || !m_audioPlaying)
        return;
    if (m_metrics.firstPlaybackAt < 0) {
        m_metrics.firstPlaybackAt = elapsedMs();
        qInfo().noquote() << QStringLiteral(
            "Voice interaction session=%1 stage=Playback event=first_audio speech_latency_ms=%2")
                .arg(sessionId)
                .arg(m_metrics.recordingEndedAt < 0 ? -1
                    : m_metrics.firstPlaybackAt - m_metrics.recordingEndedAt);
    }
    publish(VoiceInteractionState::Speaking, QStringLiteral("正在回答"));
}

void VoiceInteractionService::handlePlaybackFinished(quint64 sessionId)
{
    if (!acceptsSession(sessionId) || !m_audioPlaying)
        return;
    m_audioPlaying = false;
    m_snapshot.playbackActive = false;
    pumpPlayback();
    pumpTts();
    if (!m_audioPlaying && m_snapshot.generationActive) {
        m_snapshot.statusMessage = QStringLiteral("正在继续生成回答");
        emit snapshotChanged(m_snapshot);
    }
    maybeCompleteInteraction();
}

void VoiceInteractionService::handleAudioCancellationFinished(quint64 sessionId)
{
    if (sessionId != m_audioCancellationSessionId)
        return;
    m_audioCancellationSessionId = 0;
    if (!m_restartPending || sessionId != m_restartWaitingSessionId)
        return;
    m_restartPending = false;
    m_restartWaitingSessionId = 0;
    QTimer::singleShot(0, this, [this] {
        if (!m_snapshot.isActive())
            beginInteraction();
    });
}

void VoiceInteractionService::handleAsrFailure(
    quint64 sessionId, const AiProviderError& error)
{
    if (!acceptsSession(sessionId))
        return;
    fail(QStringLiteral("ASR"), error.userMessage,
         QStringLiteral("code=%1 provider=%2 http=%3 api_code=%4 %5")
             .arg(aiProviderErrorCodeName(error.code), error.provider)
             .arg(error.httpStatus).arg(error.apiCode, error.diagnostic),
         terminalStateFor(error));
}

void VoiceInteractionService::handleLlmFailure(
    quint64 sessionId, const AiProviderError& error)
{
    if (!acceptsSession(sessionId))
        return;
    fail(QStringLiteral("LLM"), error.userMessage,
         QStringLiteral("code=%1 provider=%2 http=%3 api_code=%4 %5")
             .arg(aiProviderErrorCodeName(error.code), error.provider)
             .arg(error.httpStatus).arg(error.apiCode, error.diagnostic),
         terminalStateFor(error));
}

void VoiceInteractionService::handleTtsFailure(
    quint64 sessionId, const AiProviderError& error)
{
    if (!acceptsSession(sessionId) || !m_ttsInFlight)
        return;
    const qint64 duration = elapsedMs() - m_currentTtsStartedAt;
    if (m_metrics.firstTtsDurationMs < 0)
        m_metrics.firstTtsDurationMs = duration;
    ++m_ttsFailureCount;
    m_ttsInFlight = false;
    m_currentTtsText.clear();
    m_currentTtsStartedAt = -1;
    qWarning().noquote() << QStringLiteral(
        "Voice interaction session=%1 stage=TTS event=request_failed code=%2 provider=%3 http=%4 diagnostic=%5")
            .arg(sessionId)
            .arg(aiProviderErrorCodeName(error.code), error.provider)
            .arg(error.httpStatus).arg(error.diagnostic);
    m_snapshot.errorMessage = QStringLiteral("部分语音生成失败，已继续回答");
    emit snapshotChanged(m_snapshot);
    pumpTts();
    maybeCompleteInteraction();
}

void VoiceInteractionService::handleAudioFailure(
    quint64 sessionId, VoiceAudioStage stage, const QString& userMessage,
    const QString& diagnostic)
{
    if (!acceptsSession(sessionId))
        return;
    fail(stage == VoiceAudioStage::Recording
             ? QStringLiteral("Recording") : QStringLiteral("Playback"),
         userMessage, diagnostic);
}

void VoiceInteractionService::maybeCompleteInteraction()
{
    if (!m_snapshot.isActive() || !m_llmFinished || m_ttsInFlight
        || !m_ttsTextQueue.isEmpty() || m_audioPlaying
        || !m_playbackQueue.isEmpty()) {
        return;
    }
    appendHistory(m_snapshot.transcript, m_snapshot.response.trimmed());
    releaseResources();
    m_snapshot.generationActive = false;
    m_snapshot.playbackActive = false;
    const bool speechFailed = m_ttsSuccessCount == 0 && m_ttsFailureCount > 0;
    m_snapshot.state = VoiceInteractionState::Idle;
    m_snapshot.statusMessage = speechFailed
        ? QStringLiteral("回答已显示，语音播放失败")
        : m_ttsFailureCount > 0
            ? QStringLiteral("回答完成，部分语音未能播放")
            : QStringLiteral("回答完成");
    m_snapshot.errorMessage = speechFailed
        ? QStringLiteral("语音服务暂时不可用，可以继续文字对话")
        : m_ttsFailureCount > 0
            ? QStringLiteral("部分语音生成失败") : QString();
    logFinalMetrics(m_ttsFailureCount > 0
        ? QStringLiteral("completed_with_tts_error")
        : QStringLiteral("completed"));
    emit snapshotChanged(m_snapshot);
}

QList<AiChatMessage> VoiceInteractionService::messagesFor(
    const QString& userText) const
{
    QList<AiChatMessage> messages;
    if (!m_configuration.voice.systemPrompt.trimmed().isEmpty()) {
        messages.append({QStringLiteral("system"),
                         m_configuration.voice.systemPrompt.trimmed()});
    }
    // 有有效天气快照时追加一条 system 上下文，让 LLM 能据实回答当前天气。
    // 没有有效数据时保持原样，绝不主动向 QWeather 发请求。
    if (m_weatherProvider) {
        const auto snapshot = m_weatherProvider();
        if (snapshot && snapshot->valid)
            messages.append({QStringLiteral("system"),
                             weatherContextMessage(*snapshot)});
    }
    messages.append(m_history);
    messages.append({QStringLiteral("user"), userText});
    return messages;
}

QString VoiceInteractionService::weatherContextMessage(
    const WeatherSnapshot& snapshot) const
{
    QString text = QStringLiteral("设备当前缓存的实时天气信息：\n");
    if (!snapshot.condition.isEmpty())
        text += QStringLiteral("天气：%1\n").arg(snapshot.condition);
    text += QStringLiteral("温度：%1℃\n").arg(QString::number(snapshot.temperatureC, 'f', 0));
    if (snapshot.feelsLikeC != 0.0)
        text += QStringLiteral("体感温度：%1℃\n").arg(QString::number(snapshot.feelsLikeC, 'f', 0));
    if (snapshot.humidityPercent > 0)
        text += QStringLiteral("湿度：%1%\n").arg(snapshot.humidityPercent);
    if (snapshot.updatedAt.isValid())
        // 内部统一存 UTC，展示给模型时转换成本机时区。
        text += QStringLiteral("数据更新时间：%1\n")
            .arg(snapshot.updatedAt.toLocalTime().toString(
                QStringLiteral("yyyy-MM-dd HH:mm")));
    if (snapshot.stale)
        text += QStringLiteral("注意：该天气数据已经超过正常更新时间，可能已经过期。\n");
    text += QStringLiteral(
        "这是一份当前实时天气数据，不包含未来天气预报。\n"
        "当用户询问当前天气时请以此数据为准。\n"
        "如果用户询问今天稍后、明天、是否会下雨、最高最低温等预测信息，\n"
        "而当前上下文没有预报数据，请明确说明没有足够的天气预报信息，不要自行猜测。");
    return text;
}

void VoiceInteractionService::appendHistory(const QString& userText,
                                            const QString& assistantText)
{
    if (m_configuration.voice.historyTurns <= 0
        || userText.trimmed().isEmpty() || assistantText.trimmed().isEmpty()) {
        return;
    }
    m_history.append({QStringLiteral("user"), userText});
    m_history.append({QStringLiteral("assistant"), assistantText});
    const int maximumMessages = m_configuration.voice.historyTurns * 2;
    while (m_history.size() > maximumMessages)
        m_history.removeFirst();
}

void VoiceInteractionService::resetSessionWork()
{
    m_recordingDeadline.stop();
    m_uiUpdateTimer.stop();
    m_sentenceBuffer.clear();
    m_vad.reset();
    m_ttsTextQueue.clear();
    m_playbackQueue.clear();
    m_metrics = {};
    m_currentTtsText.clear();
    m_currentTtsStartedAt = -1;
    m_lastCapturedMs = 0;
    m_ttsSuccessCount = 0;
    m_ttsFailureCount = 0;
    m_llmFinished = false;
    m_ttsInFlight = false;
    m_audioPlaying = false;
}

void VoiceInteractionService::cancelSession(bool restartAfterCancellation)
{
    const quint64 sessionId = m_snapshot.sessionId;
    const VoiceInteractionState previousState = m_snapshot.state;
    m_restartPending = restartAfterCancellation;
    m_restartWaitingSessionId = restartAfterCancellation ? sessionId : 0;
    m_audioCancellationSessionId = m_audio ? sessionId : 0;
    m_recordingDeadline.stop();
    m_uiUpdateTimer.stop();
    if (m_asrProvider)
        m_asrProvider->cancel(sessionId);
    if (m_llmProvider)
        m_llmProvider->cancel(sessionId);
    if (m_ttsProvider)
        m_ttsProvider->cancel(sessionId);
    qInfo().noquote() << QStringLiteral(
        "Voice interaction session=%1 event=cancelled phase=%2 restart=%3")
            .arg(sessionId).arg(static_cast<int>(previousState))
            .arg(restartAfterCancellation);
    logFinalMetrics(QStringLiteral("cancelled"));
    resetSessionWork();
    releaseResources();
    m_snapshot.generationActive = false;
    m_snapshot.playbackActive = false;
    m_snapshot.state = VoiceInteractionState::Cancelled;
    m_snapshot.statusMessage = restartAfterCancellation
        ? QStringLiteral("正在重新开始") : QStringLiteral("已停止本次对话");
    m_snapshot.errorMessage.clear();
    emit snapshotChanged(m_snapshot);
    m_snapshot.state = VoiceInteractionState::Idle;
    emit snapshotChanged(m_snapshot);
    if (m_audio)
        m_audio->cancel(sessionId);
    else if (restartAfterCancellation)
        handleAudioCancellationFinished(sessionId);
}

void VoiceInteractionService::publish(VoiceInteractionState state,
                                      const QString& statusMessage)
{
    m_snapshot.state = state;
    m_snapshot.statusMessage = statusMessage;
    m_snapshot.errorMessage.clear();
    emit snapshotChanged(m_snapshot);
}

VoiceInteractionResult VoiceInteractionService::fail(
    const QString& stage, const QString& userMessage,
    const QString& diagnostic, VoiceInteractionState terminalState)
{
    const quint64 sessionId = m_snapshot.sessionId;
    m_recordingDeadline.stop();
    m_uiUpdateTimer.stop();
    if (m_asrProvider)
        m_asrProvider->cancel(sessionId);
    if (m_llmProvider)
        m_llmProvider->cancel(sessionId);
    if (m_ttsProvider)
        m_ttsProvider->cancel(sessionId);
    if (m_audio) {
        m_audioCancellationSessionId = sessionId;
        m_audio->cancel(sessionId);
    }
    qWarning().noquote() << QStringLiteral(
        "Voice interaction session=%1 stage=%2 event=failed diagnostic=%3")
            .arg(sessionId).arg(stage, diagnostic.left(600));
    logFinalMetrics(QStringLiteral("failed_%1").arg(stage.toLower()));
    resetSessionWork();
    releaseResources();
    m_snapshot.generationActive = false;
    m_snapshot.playbackActive = false;
    m_snapshot.state = terminalState;
    m_snapshot.statusMessage = terminalState == VoiceInteractionState::Offline
        ? QStringLiteral("网络暂时不可用") : QStringLiteral("语音交互未完成");
    m_snapshot.errorMessage = userMessage.isEmpty()
        ? QStringLiteral("服务暂时不可用，请稍后再试") : userMessage;
    emit snapshotChanged(m_snapshot);
    return {false, m_snapshot.errorMessage, m_snapshot};
}

void VoiceInteractionService::logFinalMetrics(const QString& result)
{
    const qint64 total = elapsedMs();
    const qint64 ttft = m_metrics.llmFirstTokenAt < 0
        || m_metrics.llmStartedAt < 0 ? -1
        : m_metrics.llmFirstTokenAt - m_metrics.llmStartedAt;
    const qint64 llmTotal = m_metrics.llmFinishedAt < 0
        || m_metrics.llmStartedAt < 0 ? -1
        : m_metrics.llmFinishedAt - m_metrics.llmStartedAt;
    const qint64 firstSentence = m_metrics.firstSentenceAt < 0
        || m_metrics.llmStartedAt < 0 ? -1
        : m_metrics.firstSentenceAt - m_metrics.llmStartedAt;
    const qint64 speechLatency = m_metrics.firstPlaybackAt < 0
        || m_metrics.recordingEndedAt < 0 ? -1
        : m_metrics.firstPlaybackAt - m_metrics.recordingEndedAt;
    qInfo().noquote() << QStringLiteral(
        "Voice metrics session=%1 result=%2 recording_ms=%3 asr_ms=%4 llm_ttft_ms=%5 llm_total_ms=%6 first_sentence_ms=%7 first_tts_ms=%8 speech_latency_ms=%9 total_ms=%10 tts_ok=%11 tts_failed=%12")
            .arg(m_snapshot.sessionId).arg(result)
            .arg(m_metrics.recordingDurationMs).arg(m_metrics.asrDurationMs)
            .arg(ttft).arg(llmTotal).arg(firstSentence)
            .arg(m_metrics.firstTtsDurationMs).arg(speechLatency).arg(total)
            .arg(m_ttsSuccessCount).arg(m_ttsFailureCount);
}

qint64 VoiceInteractionService::elapsedMs() const
{
    return m_sessionClock.isValid() ? m_sessionClock.elapsed() : -1;
}

void VoiceInteractionService::releaseResources()
{
    if (!m_resourcesActive)
        return;
    m_resourcesActive = false;
    if (m_mediaSessions)
        m_mediaSessions->release(MediaOwner);
    emit activityChanged(false);
}
