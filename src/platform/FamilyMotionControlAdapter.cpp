#include "FamilyMotionControlAdapter.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QWebSocket>

namespace {
constexpr int ProtocolVersion = 1;
constexpr int SessionLifetimeSeconds = 30;
constexpr int AuthenticationTimeoutMs = 6'000;

QJsonObject statusObject(const MotionStatusSnapshot& status)
{
    return {
        {QStringLiteral("type"), QStringLiteral("motion_status")},
        {QStringLiteral("protocol_version"), ProtocolVersion},
        {QStringLiteral("uart_available"), status.uartAvailable},
        {QStringLiteral("mcu_online"), status.mcuOnline},
        {QStringLiteral("fault"), status.fault},
        {QStringLiteral("remote_control_active"), status.remoteControlActive},
        {QStringLiteral("automatic_head_tracking_active"),
         status.automaticHeadTrackingActive},
        {QStringLiteral("automatic_person_following_active"),
         status.automaticPersonFollowingActive},
        {QStringLiteral("mode"), motionControlModeName(status.mode)},
        {QStringLiteral("motion"), chassisMotionName(status.motion)},
        {QStringLiteral("stop_reason"), status.stopReason},
        {QStringLiteral("servo_us"), status.servoPulseUs},
        {QStringLiteral("head_offset_available"), status.headOffsetAvailable},
        {QStringLiteral("head_offset_us"), status.headOffsetUs},
        {QStringLiteral("target_available"), status.targetAvailable},
        {QStringLiteral("imu_available"), status.imuAvailable},
        {QStringLiteral("detail"), status.detail},
        {QStringLiteral("updated_at"), status.updatedAt.isValid()
             ? status.updatedAt.toUTC().toString(Qt::ISODateWithMs) : QString()}
    };
}
}

FamilyMotionControlAdapter::FamilyMotionControlAdapter(QObject* parent)
    : FamilyMotionControlPort(parent),
      m_server(QStringLiteral("LongPet family motion control"),
               QWebSocketServer::NonSecureMode, this)
{
    connect(&m_server, &QWebSocketServer::newConnection,
            this, &FamilyMotionControlAdapter::acceptPendingConnection);
    m_authenticationTimer.setSingleShot(true);
    connect(&m_authenticationTimer, &QTimer::timeout,
            this, &FamilyMotionControlAdapter::handleAuthenticationTimeout);
}

FamilyMotionControlAdapter::~FamilyMotionControlAdapter()
{
    stop();
}

bool FamilyMotionControlAdapter::start(QHostAddress address, quint16 port,
                                       QString* error)
{
    if (m_server.isListening())
        return true;
    m_stopping = false;
    if (!m_server.listen(address, port)) {
        if (error) {
            *error = QStringLiteral("远程运动控制端口 %1 监听失败：%2")
                         .arg(port).arg(m_server.errorString());
        }
        return false;
    }
    qInfo() << "Family motion WebSocket listening on"
            << m_server.serverAddress() << m_server.serverPort();
    return true;
}

void FamilyMotionControlAdapter::stop()
{
    m_stopping = true;
    m_authenticationTimer.stop();
    const bool active = m_controllerActive || m_awaitingService;
    if (m_socket) {
        disconnect(m_socket, nullptr, this, nullptr);
        m_socket->close(QWebSocketProtocol::CloseCodeNormal,
                        QStringLiteral("远程运动控制服务停止"));
        m_socket->deleteLater();
        m_socket.clear();
    }
    m_server.close();
    m_activeSession = {};
    m_activeSessionId.clear();
    m_authenticated = false;
    m_awaitingService = false;
    m_controllerActive = false;
    m_controlSequence = 0;
    resetPendingSession();
    if (active)
        emit controllerStopped(QStringLiteral("远程运动控制服务停止"));
}

quint16 FamilyMotionControlAdapter::port() const
{
    return m_server.serverPort();
}

FamilyMotionSession FamilyMotionControlAdapter::createSession(
    int refreshIntervalMs, int leaseTimeoutMs, int defaultSpeed,
    int headStepUs, QString* error)
{
    if (!m_server.isListening()) {
        if (error)
            *error = QStringLiteral("远程运动控制网络服务未监听");
        return {};
    }
    if (m_pendingSession.isValid()
        && QDateTime::currentDateTimeUtc() > m_pendingSession.expiresAt) {
        resetPendingSession();
    }
    if (m_controllerActive || m_awaitingService || m_socket
        || m_pendingSession.isValid()) {
        if (error)
            *error = QStringLiteral("已有家属正在远程控制设备");
        return {};
    }
    m_pendingSession.sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_pendingSession.token = QUuid::createUuid().toString(QUuid::WithoutBraces)
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_pendingSession.port = m_server.serverPort();
    m_pendingSession.protocolVersion = ProtocolVersion;
    m_pendingSession.refreshIntervalMs = refreshIntervalMs;
    m_pendingSession.leaseTimeoutMs = leaseTimeoutMs;
    m_pendingSession.defaultSpeed = defaultSpeed;
    m_pendingSession.headStepUs = headStepUs;
    m_pendingSession.expiresAt = QDateTime::currentDateTimeUtc()
        .addSecs(SessionLifetimeSeconds);
    return m_pendingSession;
}

void FamilyMotionControlAdapter::acceptController(const QString& sessionId)
{
    if (!m_socket || !m_awaitingService || sessionId != m_activeSessionId)
        return;
    m_awaitingService = false;
    m_controllerActive = true;
    sendControl({
        {QStringLiteral("type"), QStringLiteral("control_started")},
        {QStringLiteral("protocol_version"), ProtocolVersion},
        {QStringLiteral("refresh_interval_ms"), m_activeSession.refreshIntervalMs},
        {QStringLiteral("lease_timeout_ms"), m_activeSession.leaseTimeoutMs},
        {QStringLiteral("default_speed"), m_activeSession.defaultSpeed},
        {QStringLiteral("head_step_us"), m_activeSession.headStepUs}
    });
    resetPendingSession();
}

void FamilyMotionControlAdapter::rejectController(const QString& sessionId,
                                                  const QString& code,
                                                  const QString& message)
{
    if (!m_socket || (!sessionId.isEmpty() && sessionId != m_activeSessionId))
        return;
    sendError(code, message);
    closeSocket(QWebSocketProtocol::CloseCodePolicyViolated, message);
}

void FamilyMotionControlAdapter::terminateController(const QString& code,
                                                     const QString& message)
{
    if (!m_socket)
        return;
    sendError(code, message);
    closeSocket(QWebSocketProtocol::CloseCodeGoingAway, message);
}

void FamilyMotionControlAdapter::publishStatus(
    const MotionStatusSnapshot& status)
{
    if (m_controllerActive && m_socket)
        sendControl(statusObject(status));
}

bool FamilyMotionControlAdapter::hasController() const
{
    return m_controllerActive;
}

void FamilyMotionControlAdapter::acceptPendingConnection()
{
    while (m_server.hasPendingConnections()) {
        QWebSocket* socket = m_server.nextPendingConnection();
        if (m_socket || !m_pendingSession.isValid()
            || socket->requestUrl().path()
                != QStringLiteral("/motion-control/v1")) {
            socket->close(QWebSocketProtocol::CloseCodePolicyViolated,
                          QStringLiteral("远程运动控制会话不可用"));
            socket->deleteLater();
            continue;
        }
        m_socket = socket;
        connect(socket, &QWebSocket::binaryMessageReceived,
                this, &FamilyMotionControlAdapter::handleBinaryMessage);
        connect(socket, &QWebSocket::disconnected,
                this, &FamilyMotionControlAdapter::handleSocketDisconnected);
        m_authenticationTimer.start(AuthenticationTimeoutMs);
    }
}

void FamilyMotionControlAdapter::handleBinaryMessage(const QByteArray& bytes)
{
    MediaFrame frame;
    QString frameError;
    if (!MediaFrameProtocol::decode(bytes, &frame, &frameError)
        || frame.streamType != MediaStreamType::Control) {
        closeSocket(QWebSocketProtocol::CloseCodeProtocolError,
                    QStringLiteral("远程运动控制协议帧无效"));
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(frame.payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        closeSocket(QWebSocketProtocol::CloseCodeProtocolError,
                    QStringLiteral("远程运动控制消息不是 JSON 对象"));
        return;
    }
    const QJsonObject object = document.object();
    const QString type = object.value(QStringLiteral("type")).toString();
    if (!m_authenticated && type == QStringLiteral("authenticate")) {
        const bool accepted = m_pendingSession.isValid()
            && QDateTime::currentDateTimeUtc() <= m_pendingSession.expiresAt
            && object.value(QStringLiteral("protocol_version")).toInt()
                == ProtocolVersion
            && object.value(QStringLiteral("session_id")).toString()
                == m_pendingSession.sessionId
            && object.value(QStringLiteral("token")).toString()
                == m_pendingSession.token;
        if (!accepted) {
            rejectController({}, QStringLiteral("AUTHENTICATION_FAILED"),
                             QStringLiteral("远程运动控制鉴权失败"));
            return;
        }
        m_authenticationTimer.stop();
        m_authenticated = true;
        m_awaitingService = true;
        m_activeSession = m_pendingSession;
        m_activeSessionId = m_pendingSession.sessionId;
        emit controllerStartRequested(m_activeSessionId);
        return;
    }
    if (!m_authenticated || !m_controllerActive) {
        closeSocket(QWebSocketProtocol::CloseCodePolicyViolated,
                    QStringLiteral("远程运动控制尚未就绪"));
        return;
    }

    if (type == QStringLiteral("chassis")) {
        ChassisMotion motion = ChassisMotion::Unknown;
        const QJsonValue speedValue = object.value(QStringLiteral("speed"));
        const int speed = speedValue.toInt(-1);
        if (!chassisMotionFromName(
                object.value(QStringLiteral("direction")).toString(), &motion)
            || motion == ChassisMotion::Stopped
            || !speedValue.isDouble() || speed < 1 || speed > 100) {
            sendError(QStringLiteral("INVALID_COMMAND"),
                      QStringLiteral("底盘方向或速度无效"));
            return;
        }
        emit chassisCommandRequested(motion, speed);
        return;
    }
    if (type == QStringLiteral("head")) {
        HeadMotion motion = HeadMotion::Center;
        if (!headMotionFromName(
                object.value(QStringLiteral("action")).toString(), &motion)) {
            sendError(QStringLiteral("INVALID_COMMAND"),
                      QStringLiteral("头部动作无效"));
            return;
        }
        const int step = motion == HeadMotion::Center ? 0
            : object.value(QStringLiteral("step_us")).toInt(-1);
        if (motion != HeadMotion::Center && (step < 1 || step > 100)) {
            sendError(QStringLiteral("INVALID_COMMAND"),
                      QStringLiteral("头部步长必须为 1 到 100 us"));
            return;
        }
        emit headCommandRequested(motion, step);
        return;
    }
    if (type == QStringLiteral("stop")) {
        emit stopRequested();
        return;
    }
    if (type == QStringLiteral("release")) {
        emit stopRequested();
        closeSocket(QWebSocketProtocol::CloseCodeNormal,
                    QStringLiteral("家属端已退出远程控制"));
        return;
    }
    sendError(QStringLiteral("INVALID_COMMAND"),
              QStringLiteral("不支持的远程运动控制消息"));
}

void FamilyMotionControlAdapter::handleSocketDisconnected()
{
    QWebSocket* socket = qobject_cast<QWebSocket*>(sender());
    if (socket)
        socket->deleteLater();
    const bool active = m_controllerActive || m_awaitingService;
    m_socket.clear();
    m_authenticationTimer.stop();
    m_activeSession = {};
    m_activeSessionId.clear();
    m_authenticated = false;
    m_awaitingService = false;
    m_controllerActive = false;
    resetPendingSession();
    if (!m_stopping && active)
        emit controllerStopped(QStringLiteral("远程运动控制连接已中断"));
}

void FamilyMotionControlAdapter::handleAuthenticationTimeout()
{
    rejectController({}, QStringLiteral("AUTHENTICATION_TIMEOUT"),
                     QStringLiteral("远程运动控制鉴权超时"));
}

void FamilyMotionControlAdapter::sendControl(const QJsonObject& object)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
        return;
    m_socket->sendBinaryMessage(MediaFrameProtocol::encode(
        MediaStreamType::Control, ++m_controlSequence,
        static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1'000,
        QJsonDocument(object).toJson(QJsonDocument::Compact)));
}

void FamilyMotionControlAdapter::sendError(const QString& code,
                                           const QString& message)
{
    sendControl({
        {QStringLiteral("type"), QStringLiteral("error")},
        {QStringLiteral("code"), code},
        {QStringLiteral("message"), message}
    });
}

void FamilyMotionControlAdapter::closeSocket(
    QWebSocketProtocol::CloseCode code, const QString& reason)
{
    if (m_socket)
        m_socket->close(code, reason.left(120));
}

void FamilyMotionControlAdapter::resetPendingSession()
{
    m_pendingSession = {};
}
