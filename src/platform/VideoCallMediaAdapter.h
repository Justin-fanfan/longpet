#pragma once

#include "model/MediaFrameProtocol.h"
#include "services/VideoCallPorts.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QPointer>
#include <QProcess>
#include <QJsonObject>
#include <QQueue>
#include <QTimer>
#include <QWebSocketServer>

class QWebSocket;

class VideoCallMediaAdapter final : public VideoCallMediaPort {
    Q_OBJECT

public:
    explicit VideoCallMediaAdapter(QObject* parent = nullptr);
    ~VideoCallMediaAdapter() override;

    quint16 port() const override;
    bool prepare(const VideoCallSnapshot& snapshot,
                 bool audioEnabled,
                 QString* error = nullptr) override;
    void enableAudio() override;
    void stop() override;

private:
    void acceptPendingConnection();
    void handleBinaryMessage(const QByteArray& message);
    void handleControlFrame(const QByteArray& payload);
    void handleSocketDisconnected();
    void processCameraOutput();
    void processAudioCaptureOutput();
    void scheduleRemoteVideoDecode(const QByteArray& jpeg);
    void sendFrame(MediaStreamType streamType, const QByteArray& payload,
                   quint16 flags = 0);
    void sendControl(const QString& type, const QJsonObject& extra = {});
    void flushPendingVideo();
    void drainAudioPlayback();
    bool startCamera(QString* error);
    bool startAudio(QString* error);
    bool startProcess(QProcess* process, const QStringList& arguments,
                      const QString& label, QString* error);
    void stopProcess(QProcess* process);
    void checkReady();
    void reportProcessFailure(const QString& code, const QString& label,
                              QProcess* process, int exitCode);
    static quint16 configuredPort();

    QWebSocketServer m_server;
    QPointer<QWebSocket> m_socket;
    QProcess m_cameraProcess;
    QProcess m_captureProcess;
    QProcess m_playbackProcess;
    QTimer m_videoFlushTimer;
    QTimer m_audioPlaybackTimer;
    VideoCallSnapshot m_session;
    QByteArray m_cameraBuffer;
    QByteArray m_captureBuffer;
    QByteArray m_pendingLocalVideo;
    QByteArray m_pendingRemoteVideo;
    QQueue<QByteArray> m_audioPlaybackQueue;
    quint32 m_sequences[6] {};
    QElapsedTimer m_remoteVideoDecodeClock;
    int m_cameraFrameCounter = 0;
    bool m_prepared = false;
    bool m_authenticated = false;
    bool m_audioEnabled = false;
    bool m_audioStarted = false;
    bool m_readyEmitted = false;
    bool m_remoteDecodeScheduled = false;
    bool m_playbackPrimed = false;
    bool m_stopping = false;
};
