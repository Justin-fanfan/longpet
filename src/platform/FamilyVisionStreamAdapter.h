#pragma once

#include "model/MediaFrameProtocol.h"
#include "services/FamilyVisionPorts.h"

#include <QPointer>
#include <QTimer>
#include <QWebSocketServer>

class QWebSocket;

class FamilyVisionStreamAdapter final : public FamilyVisionStreamPort {
    Q_OBJECT

public:
    explicit FamilyVisionStreamAdapter(QObject* parent = nullptr);
    ~FamilyVisionStreamAdapter() override;

    bool start(QHostAddress address, quint16 port,
               QString* error = nullptr) override;
    void stop() override;
    quint16 port() const override;
    FamilyVisionSession createSession(int frameRate,
                                       QString* error = nullptr) override;
    void acceptViewer(const QString& sessionId) override;
    void rejectViewer(const QString& sessionId,
                      const QString& code,
                      const QString& message) override;
    void publishCameraFrame(const CameraFrame& frame) override;
    void publishTelemetry(const FamilyVisionTelemetry& telemetry) override;
    bool hasViewer() const override;

private:
    void acceptPendingConnection();
    void handleBinaryMessage(const QByteArray& bytes);
    void handleSocketDisconnected();
    void handleAuthenticationTimeout();
    void flushPendingFrame();
    void closeSocket(QWebSocketProtocol::CloseCode code, const QString& reason);
    void sendControl(const QJsonObject& object);
    void sendFrame(MediaStreamType streamType, quint32 sequence,
                   quint64 timestampUsec, const QByteArray& payload);
    void resetPendingSession();

    QWebSocketServer m_server;
    QPointer<QWebSocket> m_socket;
    QTimer m_authenticationTimer;
    QTimer m_flushTimer;
    FamilyVisionSession m_pendingSession;
    CameraFrame m_pendingFrame;
    QString m_activeSessionId;
    quint32 m_controlSequence = 0;
    bool m_authenticated = false;
    bool m_awaitingService = false;
    bool m_viewerActive = false;
    bool m_stopping = false;
};
