#include "FamilyVisionStreamAdapter.h"

#include "model/MediaFrameProtocol.h"
#include "platform/FamilyVisionProtocol.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QWebSocket>

namespace {
constexpr qint64 MaximumSocketBacklog = 512 * 1024;
constexpr int SessionLifetimeSeconds = 30;
constexpr int AuthenticationTimeoutMs = 6'000;
}

FamilyVisionStreamAdapter::FamilyVisionStreamAdapter(QObject* parent)
    : FamilyVisionStreamPort(parent),
      m_server(QStringLiteral("LongPet family AI view"),
               QWebSocketServer::NonSecureMode, this)
{
    connect(&m_server, &QWebSocketServer::newConnection,
            this, &FamilyVisionStreamAdapter::acceptPendingConnection);
    m_authenticationTimer.setSingleShot(true);
    connect(&m_authenticationTimer, &QTimer::timeout,
            this, &FamilyVisionStreamAdapter::handleAuthenticationTimeout);
    m_flushTimer.setInterval(20);
    connect(&m_flushTimer, &QTimer::timeout,
            this, &FamilyVisionStreamAdapter::flushPendingFrame);
}

FamilyVisionStreamAdapter::~FamilyVisionStreamAdapter()
{
    stop();
}

bool FamilyVisionStreamAdapter::start(QHostAddress address, quint16 port,
                                      QString* error)
{
    if (m_server.isListening())
        return true;
    m_stopping = false;
    if (!m_server.listen(address, port)) {
        if (error) {
            *error = QStringLiteral("AI 视野端口 %1 监听失败：%2")
                         .arg(port).arg(m_server.errorString());
        }
        return false;
    }
    m_flushTimer.start();
    qInfo() << "Family AI View WebSocket listening on"
            << m_server.serverAddress() << m_server.serverPort();
    return true;
}

void FamilyVisionStreamAdapter::stop()
{
    m_stopping = true;
    m_authenticationTimer.stop();
    m_flushTimer.stop();
    const bool active = m_viewerActive || m_awaitingService;
    if (m_socket) {
        disconnect(m_socket, nullptr, this, nullptr);
        m_socket->close(QWebSocketProtocol::CloseCodeNormal,
                        QStringLiteral("AI 视野服务停止"));
        m_socket->deleteLater();
        m_socket.clear();
    }
    m_server.close();
    m_pendingFrame = {};
    m_activeSessionId.clear();
    m_authenticated = false;
    m_awaitingService = false;
    m_viewerActive = false;
    m_controlSequence = 0;
    resetPendingSession();
    if (active)
        emit viewerStopped(QStringLiteral("AI 视野服务停止"));
}

quint16 FamilyVisionStreamAdapter::port() const
{
    return m_server.serverPort();
}

FamilyVisionSession FamilyVisionStreamAdapter::createSession(int frameRate,
                                                              QString* error)
{
    if (!m_server.isListening()) {
        if (error)
            *error = QStringLiteral("AI 视野网络服务未监听");
        return {};
    }
    if (m_viewerActive || m_awaitingService || m_socket) {
        if (error)
            *error = QStringLiteral("已有家属正在查看 AI 视野");
        return {};
    }
    m_pendingSession.sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_pendingSession.token = QUuid::createUuid().toString(QUuid::WithoutBraces)
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_pendingSession.port = m_server.serverPort();
    m_pendingSession.protocolVersion = FamilyVisionProtocol::Version;
    m_pendingSession.frameRate = frameRate;
    m_pendingSession.expiresAt = QDateTime::currentDateTimeUtc()
        .addSecs(SessionLifetimeSeconds);
    return m_pendingSession;
}

void FamilyVisionStreamAdapter::acceptViewer(const QString& sessionId,
                                             int cameraRotationDegrees)
{
    if (!m_socket || !m_awaitingService || sessionId != m_activeSessionId)
        return;
    m_awaitingService = false;
    m_viewerActive = true;
    sendControl({
        {QStringLiteral("type"), QStringLiteral("stream_started")},
        {QStringLiteral("protocol_version"), FamilyVisionProtocol::Version},
        {QStringLiteral("frame_rate"), m_pendingSession.frameRate},
        {QStringLiteral("camera_rotation"), cameraRotationDegrees}
    });
    resetPendingSession();
}

void FamilyVisionStreamAdapter::rejectViewer(const QString& sessionId,
                                             const QString& code,
                                             const QString& message)
{
    if (!m_socket || (!sessionId.isEmpty() && sessionId != m_activeSessionId))
        return;
    sendControl({
        {QStringLiteral("type"), QStringLiteral("error")},
        {QStringLiteral("code"), code},
        {QStringLiteral("message"), message}
    });
    closeSocket(QWebSocketProtocol::CloseCodePolicyViolated, message);
}

void FamilyVisionStreamAdapter::publishCameraFrame(const CameraFrame& frame)
{
    if (!m_viewerActive || !m_socket || !frame.isValid())
        return;
    if (m_socket->bytesToWrite() >= MaximumSocketBacklog) {
        m_pendingFrame = frame;
        return;
    }
    const quint64 timestampUsec = frame.timestamp.isValid()
        ? static_cast<quint64>(frame.timestamp.toMSecsSinceEpoch()) * 1'000
        : static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1'000;
    sendFrame(MediaStreamType::DeviceVideo,
              static_cast<quint32>(frame.sequence), timestampUsec, frame.jpeg);
}

void FamilyVisionStreamAdapter::publishTelemetry(
    const FamilyVisionTelemetry& telemetry)
{
    if (!m_viewerActive || !m_socket)
        return;
    sendControl(FamilyVisionProtocol::telemetryObject(telemetry));
}

bool FamilyVisionStreamAdapter::hasViewer() const
{
    return m_viewerActive;
}

void FamilyVisionStreamAdapter::acceptPendingConnection()
{
    while (m_server.hasPendingConnections()) {
        QWebSocket* socket = m_server.nextPendingConnection();
        if (m_socket || socket->requestUrl().path()
                != QStringLiteral("/vision-monitor/v1")) {
            socket->close(QWebSocketProtocol::CloseCodePolicyViolated,
                          QStringLiteral("AI 视野会话不可用"));
            socket->deleteLater();
            continue;
        }
        m_socket = socket;
        connect(socket, &QWebSocket::binaryMessageReceived,
                this, &FamilyVisionStreamAdapter::handleBinaryMessage);
        connect(socket, &QWebSocket::disconnected,
                this, &FamilyVisionStreamAdapter::handleSocketDisconnected);
        m_authenticationTimer.start(AuthenticationTimeoutMs);
    }
}

void FamilyVisionStreamAdapter::handleBinaryMessage(const QByteArray& bytes)
{
    MediaFrame frame;
    QString error;
    if (!MediaFrameProtocol::decode(bytes, &frame, &error)
        || frame.streamType != MediaStreamType::Control) {
        closeSocket(QWebSocketProtocol::CloseCodeProtocolError,
                    QStringLiteral("AI 视野协议帧无效"));
        return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(frame.payload);
    if (!document.isObject()) {
        closeSocket(QWebSocketProtocol::CloseCodeProtocolError,
                    QStringLiteral("AI 视野控制消息无效"));
        return;
    }
    const QJsonObject object = document.object();
    const QString type = object.value(QStringLiteral("type")).toString();
    if (!m_authenticated && type == QStringLiteral("authenticate")) {
        const bool accepted = m_pendingSession.isValid()
            && QDateTime::currentDateTimeUtc() <= m_pendingSession.expiresAt
            && object.value(QStringLiteral("protocol_version")).toInt()
                == FamilyVisionProtocol::Version
            && object.value(QStringLiteral("session_id")).toString()
                == m_pendingSession.sessionId
            && object.value(QStringLiteral("token")).toString()
                == m_pendingSession.token;
        if (!accepted) {
            closeSocket(QWebSocketProtocol::CloseCodePolicyViolated,
                        QStringLiteral("AI 视野鉴权失败"));
            return;
        }
        m_authenticationTimer.stop();
        m_authenticated = true;
        m_awaitingService = true;
        m_activeSessionId = m_pendingSession.sessionId;
        emit viewerStartRequested(m_activeSessionId);
        return;
    }
    if (!m_authenticated) {
        closeSocket(QWebSocketProtocol::CloseCodePolicyViolated,
                    QStringLiteral("AI 视野鉴权尚未完成"));
        return;
    }
    if (type == QStringLiteral("stop")) {
        closeSocket(QWebSocketProtocol::CloseCodeNormal,
                    QStringLiteral("家属端已退出 AI 视野"));
    }
}

void FamilyVisionStreamAdapter::handleSocketDisconnected()
{
    QWebSocket* socket = qobject_cast<QWebSocket*>(sender());
    if (socket)
        socket->deleteLater();
    const bool active = m_viewerActive || m_awaitingService;
    m_socket.clear();
    m_authenticationTimer.stop();
    m_pendingFrame = {};
    m_activeSessionId.clear();
    m_authenticated = false;
    m_awaitingService = false;
    m_viewerActive = false;
    resetPendingSession();
    if (!m_stopping && active)
        emit viewerStopped(QStringLiteral("AI 视野网络连接已中断"));
}

void FamilyVisionStreamAdapter::handleAuthenticationTimeout()
{
    closeSocket(QWebSocketProtocol::CloseCodePolicyViolated,
                QStringLiteral("AI 视野鉴权超时"));
}

void FamilyVisionStreamAdapter::flushPendingFrame()
{
    if (!m_pendingFrame.isValid() || !m_socket
        || m_socket->bytesToWrite() >= MaximumSocketBacklog) {
        return;
    }
    const CameraFrame latest = m_pendingFrame;
    m_pendingFrame = {};
    publishCameraFrame(latest);
}

void FamilyVisionStreamAdapter::closeSocket(QWebSocketProtocol::CloseCode code,
                                            const QString& reason)
{
    if (m_socket)
        m_socket->close(code, reason);
}

void FamilyVisionStreamAdapter::sendControl(const QJsonObject& object)
{
    sendFrame(MediaStreamType::Control, ++m_controlSequence,
              static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1'000,
              QJsonDocument(object).toJson(QJsonDocument::Compact));
}

void FamilyVisionStreamAdapter::sendFrame(MediaStreamType streamType,
                                          quint32 sequence,
                                          quint64 timestampUsec,
                                          const QByteArray& payload)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
        return;
    m_socket->sendBinaryMessage(MediaFrameProtocol::encode(
        streamType, sequence, timestampUsec, payload));
}

void FamilyVisionStreamAdapter::resetPendingSession()
{
    m_pendingSession = {};
}
