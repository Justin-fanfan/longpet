#pragma once

#include "services/VoiceInteractionPorts.h"

#include <QByteArray>
#include <QProcess>
#include <QTimer>

class VoiceAudioAdapter final : public VoiceAudioPort {
    Q_OBJECT

public:
    explicit VoiceAudioAdapter(QObject* parent = nullptr);
    ~VoiceAudioAdapter() override;

    void startRecording(quint64 sessionId) override;
    void finishRecording(quint64 sessionId) override;
    void play(quint64 sessionId, const QByteArray& audio) override;
    void cancel(quint64 sessionId) override;

    static QByteArray pcmS16LeMonoToWav(const QByteArray& pcm,
                                        quint32 sampleRate = 16'000);
    static double pcmS16LeLevelDb(const QByteArray& pcm);

private:
    void configureProcess(QProcess* process);
    void handleCaptureFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handlePlaybackFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void reportCaptureFailure(const QString& diagnostic);
    void reportPlaybackFailure(const QString& diagnostic);
    void maybeCompleteCancellation();
    void resetCapture();
    void resetPlayback();

    QProcess m_captureProcess;
    QProcess m_playbackProcess;
    QTimer m_captureKillTimer;
    QTimer m_playbackKillTimer;
    QByteArray m_pcm;
    QByteArray m_playbackAudio;
    quint64 m_captureSessionId = 0;
    quint64 m_playbackSessionId = 0;
    quint64 m_pendingCancelSessionId = 0;
    bool m_finishingCapture = false;
    bool m_cancelingCapture = false;
    bool m_cancelingPlayback = false;
    bool m_captureFailureReported = false;
    bool m_playbackFailureReported = false;
};
