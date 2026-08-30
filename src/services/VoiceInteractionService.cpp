#include "VoiceInteractionService.h"

#include "services/MediaSessionCoordinator.h"
#include "services/VoiceInteractionPorts.h"

#include <QDebug>

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
      m_mediaSessions(mediaSessions)
{
    m_recordingDeadline.setSingleShot(true);
    m_recordingDeadline.setInterval(qMax(1'000, configuration.voice.recordingMaximumMs));
    connect(&m_recordingDeadline, &QTimer::timeout,
            this, [this] { finishRecording(); });

    if (m_audio) {
        connect(m_audio, &VoiceAudioPort::recordingStarted,
                this, &VoiceInteractionService::handleRecordingStarted);
        connect(m_audio, &VoiceAudioPort::recordingReady,
                this, &VoiceInteractionService::handleRecordingReady);
        connect(m_audio, &VoiceAudioPort::playbackStarted,
                this, &VoiceInteractionService::handlePlaybackStarted);
        connect(m_audio, &VoiceAudioPort::playbackFinished,
                this, &VoiceInteractionService::handlePlaybackFinished);
        connect(m_audio, &VoiceAudioPort::audioFailed,
                this, &VoiceInteractionService::handleAudioFailure);
    }
    if (m_asrProvider) {
        connect(m_asrProvider, &AsrProviderPort::transcriptionReady,
                this, &VoiceInteractionService::handleTranscription);
        connect(m_asrProvider, &AsrProviderPort::requestFailed,
                this, &VoiceInteractionService::handleProviderFailure);
    }
    if (m_llmProvider) {
        connect(m_llmProvider, &LlmProviderPort::chatCompletionReady,
                this, &VoiceInteractionService::handleChatCompletion);
        connect(m_llmProvider, &LlmProviderPort::requestFailed,
                this, &VoiceInteractionService::handleProviderFailure);
    }
    if (m_ttsProvider) {
        connect(m_ttsProvider, &TtsProviderPort::speechReady,
                this, &VoiceInteractionService::handleSpeech);
        connect(m_ttsProvider, &TtsProviderPort::requestFailed,
                this, &VoiceInteractionService::handleProviderFailure);
    }
}

VoiceInteractionService::~VoiceInteractionService()
{
    if (m_snapshot.isActive()) {
        if (m_asrProvider)
            m_asrProvider->cancel(m_snapshot.sessionId);
        if (m_llmProvider)
            m_llmProvider->cancel(m_snapshot.sessionId);
        if (m_ttsProvider)
            m_ttsProvider->cancel(m_snapshot.sessionId);
        if (m_audio)
            m_audio->cancel(m_snapshot.sessionId);
    }
    releaseResources();
}

VoiceInteractionSnapshot VoiceInteractionService::snapshot() const
{
    return m_snapshot;
}

VoiceInteractionResult VoiceInteractionService::startInteraction()
{
    if (m_snapshot.isActive())
        return {false, QStringLiteral("语音交互正在进行"), m_snapshot};

    m_snapshot = {};
    m_snapshot.sessionId = ++m_nextSessionId;
    const QString configurationError = m_configuration.validationError();
    if (!configurationError.isEmpty())
        return fail(configurationError);
    if (!m_asrProvider || !m_llmProvider || !m_ttsProvider || !m_audio)
        return fail(QStringLiteral("语音服务尚未准备好"));
    if (m_mediaSessions && !m_mediaSessions->tryAcquire(MediaOwner))
        return fail(QStringLiteral("设备正在通话，请稍后再试"));

    m_resourcesActive = true;
    emit activityChanged(true);
    publish(VoiceInteractionState::Recording, QStringLiteral("正在打开麦克风"));
    m_recordingDeadline.start();
    m_audio->startRecording(m_snapshot.sessionId);
    return {true, {}, m_snapshot};
}

VoiceInteractionResult VoiceInteractionService::finishRecording()
{
    if (m_snapshot.state != VoiceInteractionState::Recording) {
        return {false, QStringLiteral("当前没有正在进行的录音"), m_snapshot};
    }
    m_recordingDeadline.stop();
    publish(VoiceInteractionState::Recognizing, QStringLiteral("正在整理录音"));
    m_audio->finishRecording(m_snapshot.sessionId);
    return {true, {}, m_snapshot};
}

VoiceInteractionResult VoiceInteractionService::cancelInteraction()
{
    if (!m_snapshot.isActive())
        return {true, {}, m_snapshot};

    const quint64 sessionId = m_snapshot.sessionId;
    m_recordingDeadline.stop();
    if (m_asrProvider)
        m_asrProvider->cancel(sessionId);
    if (m_llmProvider)
        m_llmProvider->cancel(sessionId);
    if (m_ttsProvider)
        m_ttsProvider->cancel(sessionId);
    if (m_audio)
        m_audio->cancel(sessionId);
    releaseResources();
    m_snapshot.state = VoiceInteractionState::Idle;
    m_snapshot.statusMessage = QStringLiteral("已停止本次对话");
    m_snapshot.errorMessage.clear();
    emit snapshotChanged(m_snapshot);
    return {true, {}, m_snapshot};
}

bool VoiceInteractionService::accepts(quint64 sessionId,
                                      VoiceInteractionState state) const
{
    return sessionId == m_snapshot.sessionId && m_snapshot.state == state;
}

void VoiceInteractionService::handleRecordingStarted(quint64 sessionId)
{
    if (!accepts(sessionId, VoiceInteractionState::Recording))
        return;
    m_snapshot.statusMessage = QStringLiteral("正在聆听，请说话");
    emit snapshotChanged(m_snapshot);
}

void VoiceInteractionService::handleRecordingReady(
    quint64 sessionId, const QByteArray& wavAudio)
{
    if (!accepts(sessionId, VoiceInteractionState::Recognizing))
        return;
    if (wavAudio.size() <= 44) {
        fail(QStringLiteral("没有录到有效声音，请再试一次"),
             QStringLiteral("recording returned %1 bytes").arg(wavAudio.size()));
        return;
    }
    m_snapshot.statusMessage = QStringLiteral("正在识别");
    emit snapshotChanged(m_snapshot);
    m_asrProvider->transcribe(sessionId, wavAudio);
}

void VoiceInteractionService::handleTranscription(quint64 sessionId,
                                                   const QString& text)
{
    if (!accepts(sessionId, VoiceInteractionState::Recognizing))
        return;
    const QString transcript = text.trimmed();
    if (transcript.isEmpty()) {
        fail(QStringLiteral("没有听清，请再说一次"),
             QStringLiteral("ASR returned empty text"));
        return;
    }
    m_snapshot.transcript = transcript;
    publish(VoiceInteractionState::Thinking, QStringLiteral("正在思考"));
    m_llmProvider->completeChat(sessionId, messagesFor(transcript));
}

void VoiceInteractionService::handleChatCompletion(quint64 sessionId,
                                                   const QString& text)
{
    if (!accepts(sessionId, VoiceInteractionState::Thinking))
        return;
    const QString response = text.trimmed();
    if (response.isEmpty()) {
        fail(QStringLiteral("暂时没有得到回答，请稍后再试"),
             QStringLiteral("LLM returned empty content"));
        return;
    }
    m_snapshot.response = response;
    appendHistory(m_snapshot.transcript, response);
    m_snapshot.statusMessage = QStringLiteral("正在生成语音");
    emit snapshotChanged(m_snapshot);
    m_ttsProvider->synthesize(sessionId, response);
}

void VoiceInteractionService::handleSpeech(quint64 sessionId,
                                           const QByteArray& audio)
{
    if (!accepts(sessionId, VoiceInteractionState::Thinking))
        return;
    if (audio.isEmpty()) {
        fail(QStringLiteral("语音生成失败，请稍后再试"),
             QStringLiteral("TTS returned empty audio"));
        return;
    }
    m_snapshot.statusMessage = QStringLiteral("正在准备播放");
    emit snapshotChanged(m_snapshot);
    m_audio->play(sessionId, audio);
}

void VoiceInteractionService::handlePlaybackStarted(quint64 sessionId)
{
    if (!accepts(sessionId, VoiceInteractionState::Thinking))
        return;
    publish(VoiceInteractionState::Speaking, QStringLiteral("正在回答"));
}

void VoiceInteractionService::handlePlaybackFinished(quint64 sessionId)
{
    if (!accepts(sessionId, VoiceInteractionState::Speaking))
        return;
    releaseResources();
    publish(VoiceInteractionState::Idle, QStringLiteral("回答完成"));
}

void VoiceInteractionService::handleProviderFailure(
    quint64 sessionId, const AiProviderError& error)
{
    if (sessionId != m_snapshot.sessionId || !m_snapshot.isActive())
        return;
    const QString diagnostic = QStringLiteral("code=%1 provider=%2 %3")
        .arg(aiProviderErrorCodeName(error.code), error.provider, error.diagnostic);
    fail(error.userMessage, diagnostic);
}

void VoiceInteractionService::handleAudioFailure(
    quint64 sessionId, VoiceAudioStage, const QString& userMessage,
    const QString& diagnostic)
{
    if (sessionId != m_snapshot.sessionId || !m_snapshot.isActive())
        return;
    fail(userMessage, diagnostic);
}

QList<AiChatMessage> VoiceInteractionService::messagesFor(
    const QString& userText) const
{
    QList<AiChatMessage> messages;
    if (!m_configuration.voice.systemPrompt.trimmed().isEmpty()) {
        messages.append({QStringLiteral("system"),
                         m_configuration.voice.systemPrompt.trimmed()});
    }
    messages.append(m_history);
    messages.append({QStringLiteral("user"), userText});
    return messages;
}

void VoiceInteractionService::appendHistory(const QString& userText,
                                            const QString& assistantText)
{
    if (m_configuration.voice.historyTurns <= 0)
        return;
    m_history.append({QStringLiteral("user"), userText});
    m_history.append({QStringLiteral("assistant"), assistantText});
    const int maximumMessages = m_configuration.voice.historyTurns * 2;
    while (m_history.size() > maximumMessages)
        m_history.removeFirst();
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
    const QString& userMessage, const QString& diagnostic)
{
    const quint64 sessionId = m_snapshot.sessionId;
    m_recordingDeadline.stop();
    if (m_asrProvider)
        m_asrProvider->cancel(sessionId);
    if (m_llmProvider)
        m_llmProvider->cancel(sessionId);
    if (m_ttsProvider)
        m_ttsProvider->cancel(sessionId);
    if (m_audio)
        m_audio->cancel(sessionId);
    releaseResources();
    m_snapshot.state = VoiceInteractionState::Failed;
    m_snapshot.statusMessage = QStringLiteral("语音交互未完成");
    m_snapshot.errorMessage = userMessage.isEmpty()
        ? QStringLiteral("服务暂时不可用，请稍后再试") : userMessage;
    if (!diagnostic.isEmpty())
        qWarning().noquote() << "Voice interaction failed:" << diagnostic;
    emit snapshotChanged(m_snapshot);
    return {false, m_snapshot.errorMessage, m_snapshot};
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
