#pragma once

#include "model/AiModels.h"

#include <QObject>

class MediaSessionCoordinator;
class OfflineAudioLibraryPort;
class VoiceAudioPort;

class LocalCompanionService final : public QObject {
    Q_OBJECT

public:
    LocalCompanionService(const OfflineVoiceConfiguration& configuration,
                          OfflineAudioLibraryPort* library,
                          VoiceAudioPort* audio,
                          MediaSessionCoordinator* mediaSessions = nullptr,
                          QObject* parent = nullptr);
    ~LocalCompanionService() override;

    bool start(QString* error = nullptr);
    void stop();
    bool isActive() const;

signals:
    void activityChanged(bool active);
    void playbackStarted();
    void playbackFinished();
    void playbackFailed(const QString& userMessage,
                        const QString& diagnostic);

private:
    void playWhenReady();
    void finish(bool success, const QString& userMessage = {},
                const QString& diagnostic = {});

    static const QString MediaOwner;
    OfflineVoiceConfiguration m_configuration;
    OfflineAudioLibraryPort* m_library = nullptr;
    VoiceAudioPort* m_audio = nullptr;
    MediaSessionCoordinator* m_mediaSessions = nullptr;
    QString m_lastClipId;
    QByteArray m_pendingAudio;
    QString m_failureMessage;
    QString m_failureDiagnostic;
    quint64 m_nextSession = 0;
    quint64 m_sessionId = 0;
    bool m_active = false;
    bool m_canceling = false;
};
