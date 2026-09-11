#pragma once

#include "model/MediaFrameProtocol.h"
#include "services/MotionPorts.h"

#include <QPointer>
#include <QTimer>
#include <QWebSocketServer>

class QWebSocket;

class FamilyMotionControlAdapter final : public FamilyMotionControlPort {
    Q_OBJECT

public:
    explicit FamilyMotionControlAdapter(QObject* parent = nullptr);
    ~FamilyMotionControlAdapter() override;

    bool start(QHostAddress address, quint16 port,
               QString* error = nullptr) override;
    void stop() override;
    quint16 port() const override;
    FamilyMotionSession createSession(int refreshIntervalMs,
                                      int leaseTimeoutMs,
                                      int defaultSpeed,
                                      int headStepUs,
                                      QString* error = nullptr) override;
    void acceptController(const QString& sessionId) override;
    void rejectController(const QString& sessionId,
                          const QString& code,
                          const QString& message) override;
    void terminateController(const QString& code,
                             const QString& message) override;
    void publishStatus(const MotionStatusSnapshot& status) override;
    bool hasController() const override;

private:
    void acceptPendingConnection();
    void handleBinaryMessage(const QByteArray& bytes);
    void handleSocketDisconnected();
    void handleAuthenticationTimeout();
    void sendControl(const QJsonObject& object);
    void sendError(const QString& code, const QString& message);
    void closeSocket(QWebSocketProtocol::CloseCode code, const QString& reason);
    void resetPendingSession();

    QWebSocketServer m_server;
    QPointer<QWebSocket> m_socket;
    QTimer m_authenticationTimer;
    FamilyMotionSession m_pendingSession;
    FamilyMotionSession m_activeSession;
    QString m_activeSessionId;
    quint32 m_controlSequence = 0;
    bool m_authenticated = false;
    bool m_awaitingService = false;
    bool m_controllerActive = false;
    bool m_stopping = false;
};

