#pragma once

#include "model/AiModels.h"
#include "model/WeatherModels.h"
#include "services/SentenceBuffer.h"
#include "services/VoiceActivityDetector.h"

#include <QElapsedTimer>
#include <QObject>
#include <QQueue>
#include <QTimer>

#include <functional>
#include <optional>

class AsrProviderPort;
class LlmProviderPort;
class MediaSessionCoordinator;
class TtsProviderPort;
class VoiceAudioPort;

class VoiceInteractionService final : public QObject {
    Q_OBJECT

public:
    VoiceInteractionService(const AiConfiguration& configuration,
                            AsrProviderPort* asrProvider,
                            LlmProviderPort* llmProvider,
                            TtsProviderPort* ttsProvider,
                            VoiceAudioPort* audio,
                            MediaSessionCoordinator* mediaSessions = nullptr,
                            QObject* parent = nullptr);
    ~VoiceInteractionService() override;

    VoiceInteractionSnapshot snapshot() const;
    VoiceInteractionResult startInteraction();
    VoiceInteractionResult restartInteraction();
    VoiceInteractionResult finishRecording();
    VoiceInteractionResult cancelInteraction();
    bool clearConversationHistory();

    // 注入一个只读的天气快照入口（一般来自 WeatherService::currentOrNone）。
    // 为空时不注入天气上下文；VoiceInteractionService 不依赖具体天气 Provider。
    void setWeatherProvider(std::function<std::optional<WeatherSnapshot>()> provider);

signals:
    void snapshotChanged(const VoiceInteractionSnapshot& snapshot);
    void activityChanged(bool active);

private:
    struct PerformanceMetrics {
        qint64 recordingStartedAt = -1;
        qint64 recordingEndedAt = -1;
        qint64 recordingDurationMs = -1;
        qint64 asrStartedAt = -1;
        qint64 asrDurationMs = -1;
        qint64 llmStartedAt = -1;
        qint64 llmFirstTokenAt = -1;
        qint64 llmFinishedAt = -1;
        qint64 firstSentenceAt = -1;
        qint64 firstTtsStartedAt = -1;
        qint64 firstTtsDurationMs = -1;
        qint64 firstPlaybackAt = -1;
    };

    VoiceInteractionResult beginInteraction();
    bool acceptsSession(quint64 sessionId) const;
    void handleRecordingStarted(quint64 sessionId);
    void handleRecordingProgress(quint64 sessionId, qint64 capturedMs,
                                 double levelDb);
    void handleRecordingReady(quint64 sessionId, const QByteArray& wavAudio);
    void handleTranscription(quint64 sessionId, const QString& text);
    void handleLlmDelta(quint64 sessionId, const QString& delta);
    void handleChatCompletion(quint64 sessionId, const QString& text);
    void handleSpeech(quint64 sessionId, const QByteArray& audio);
    void handlePlaybackStarted(quint64 sessionId);
    void handlePlaybackFinished(quint64 sessionId);
    void handleAudioCancellationFinished(quint64 sessionId);
    void handleAsrFailure(quint64 sessionId, const AiProviderError& error);
    void handleLlmFailure(quint64 sessionId, const AiProviderError& error);
    void handleTtsFailure(quint64 sessionId, const AiProviderError& error);
    void handleAudioFailure(quint64 sessionId, VoiceAudioStage stage,
                            const QString& userMessage, const QString& diagnostic);
    QList<AiChatMessage> messagesFor(const QString& userText) const;
    QString weatherContextMessage(const WeatherSnapshot& snapshot) const;
    void appendHistory(const QString& userText, const QString& assistantText);
    void enqueueSentences(const QStringList& sentences);
    void pumpTts();
    void pumpPlayback();
    void maybeCompleteInteraction();
    void resetSessionWork();
    void cancelSession(bool restartAfterCancellation);
    void publish(VoiceInteractionState state, const QString& statusMessage);
    VoiceInteractionResult fail(const QString& stage,
                                const QString& userMessage,
                                const QString& diagnostic = {},
                                VoiceInteractionState terminalState =
                                    VoiceInteractionState::Error);
    void logFinalMetrics(const QString& result);
    qint64 elapsedMs() const;
    void releaseResources();

    static const QString MediaOwner;
    AiConfiguration m_configuration;
    AsrProviderPort* m_asrProvider = nullptr;
    LlmProviderPort* m_llmProvider = nullptr;
    TtsProviderPort* m_ttsProvider = nullptr;
    VoiceAudioPort* m_audio = nullptr;
    MediaSessionCoordinator* m_mediaSessions = nullptr;
    std::function<std::optional<WeatherSnapshot>()> m_weatherProvider;
    VoiceInteractionSnapshot m_snapshot;
    QList<AiChatMessage> m_history;
    SentenceBuffer m_sentenceBuffer;
    VoiceActivityDetector m_vad;
    QQueue<QString> m_ttsTextQueue;
    QQueue<QByteArray> m_playbackQueue;
    QTimer m_recordingDeadline;
    QTimer m_uiUpdateTimer;
    QElapsedTimer m_sessionClock;
    PerformanceMetrics m_metrics;
    QString m_currentTtsText;
    qint64 m_currentTtsStartedAt = -1;
    qint64 m_lastCapturedMs = 0;
    quint64 m_nextSessionId = 0;
    quint64 m_restartWaitingSessionId = 0;
    quint64 m_audioCancellationSessionId = 0;
    int m_ttsSuccessCount = 0;
    int m_ttsFailureCount = 0;
    bool m_resourcesActive = false;
    bool m_restartPending = false;
    bool m_llmFinished = false;
    bool m_ttsInFlight = false;
    bool m_audioPlaying = false;
};
