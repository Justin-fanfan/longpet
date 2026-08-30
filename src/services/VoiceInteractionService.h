#pragma once

#include "model/AiModels.h"

#include <QByteArray>
#include <QObject>
#include <QTimer>

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
    VoiceInteractionResult finishRecording();
    VoiceInteractionResult cancelInteraction();

signals:
    void snapshotChanged(const VoiceInteractionSnapshot& snapshot);
    void activityChanged(bool active);

private:
    bool accepts(quint64 sessionId, VoiceInteractionState state) const;
    void handleRecordingStarted(quint64 sessionId);
    void handleRecordingReady(quint64 sessionId, const QByteArray& wavAudio);
    void handleTranscription(quint64 sessionId, const QString& text);
    void handleChatCompletion(quint64 sessionId, const QString& text);
    void handleSpeech(quint64 sessionId, const QByteArray& audio);
    void handlePlaybackStarted(quint64 sessionId);
    void handlePlaybackFinished(quint64 sessionId);
    void handleProviderFailure(quint64 sessionId, const AiProviderError& error);
    void handleAudioFailure(quint64 sessionId, VoiceAudioStage stage,
                            const QString& userMessage, const QString& diagnostic);
    QList<AiChatMessage> messagesFor(const QString& userText) const;
    void appendHistory(const QString& userText, const QString& assistantText);
    void publish(VoiceInteractionState state, const QString& statusMessage);
    VoiceInteractionResult fail(const QString& userMessage, const QString& diagnostic = {});
    void releaseResources();

    static const QString MediaOwner;
    AiConfiguration m_configuration;
    AsrProviderPort* m_asrProvider = nullptr;
    LlmProviderPort* m_llmProvider = nullptr;
    TtsProviderPort* m_ttsProvider = nullptr;
    VoiceAudioPort* m_audio = nullptr;
    MediaSessionCoordinator* m_mediaSessions = nullptr;
    VoiceInteractionSnapshot m_snapshot;
    QList<AiChatMessage> m_history;
    QTimer m_recordingDeadline;
    quint64 m_nextSessionId = 0;
    bool m_resourcesActive = false;
};
