#include "MotionService.h"

#include "services/MotionPorts.h"

#include <QDebug>

#include <algorithm>

MotionService::MotionService(MotionPort* motionPort,
                             FamilyMotionControlPort* controlPort,
                             MotionServiceConfiguration configuration,
                             QObject* parent)
    : QObject(parent),
      m_motionPort(motionPort),
      m_controlPort(controlPort),
      m_configuration(configuration)
{
    m_configuration.moveRefreshIntervalMs = std::clamp(
        m_configuration.moveRefreshIntervalMs, 100, 200);
    m_configuration.remoteLeaseTimeoutMs = std::clamp(
        m_configuration.remoteLeaseTimeoutMs,
        m_configuration.moveRefreshIntervalMs + 50, 450);
    m_configuration.defaultSpeed = std::clamp(
        m_configuration.defaultSpeed, 1, 100);
    m_configuration.headStepUs = std::clamp(
        m_configuration.headStepUs, 1, 100);
    m_configuration.statusPollIntervalMs = std::max(
        250, m_configuration.statusPollIntervalMs);
    m_configuration.mcuOfflineTimeoutMs = std::max(
        m_configuration.statusPollIntervalMs * 2,
        m_configuration.mcuOfflineTimeoutMs);

    m_refreshTimer.setInterval(25);
    connect(&m_refreshTimer, &QTimer::timeout,
            this, &MotionService::refreshMotionLease);
    m_statusTimer.setInterval(m_configuration.statusPollIntervalMs);
    connect(&m_statusTimer, &QTimer::timeout,
            this, &MotionService::pollMcuStatus);

    if (m_motionPort) {
        connect(m_motionPort, &MotionPort::transportAvailabilityChanged,
                this, &MotionService::handleTransportAvailability);
        connect(m_motionPort, &MotionPort::mcuActivity,
                this, &MotionService::handleMcuActivity);
        connect(m_motionPort, &MotionPort::statusReceived,
                this, &MotionService::handleMcuStatus);
        connect(m_motionPort, &MotionPort::faultReported,
                this, &MotionService::handleMcuFault);
    }
    if (m_controlPort) {
        connect(m_controlPort, &FamilyMotionControlPort::controllerStartRequested,
                this, &MotionService::handleControllerStart);
        connect(m_controlPort, &FamilyMotionControlPort::controllerStopped,
                this, &MotionService::handleControllerStopped);
        connect(m_controlPort, &FamilyMotionControlPort::chassisCommandRequested,
                this, &MotionService::handleChassisCommand);
        connect(m_controlPort, &FamilyMotionControlPort::stopRequested,
                this, &MotionService::handleStopRequested);
        connect(m_controlPort, &FamilyMotionControlPort::headCommandRequested,
                this, &MotionService::handleHeadCommand);
    }
}

MotionService::~MotionService()
{
    stop();
}

bool MotionService::start(QHostAddress address, quint16 controlPort,
                          const QString& serialDevice, int baudRate,
                          QString* error)
{
    if (m_started)
        return true;
    if (!m_motionPort || !m_controlPort) {
        if (error)
            *error = QStringLiteral("Motion Port 未配置");
        return false;
    }
    QString networkError;
    if (!m_controlPort->start(address, controlPort, &networkError)) {
        if (error)
            *error = networkError;
        return false;
    }
    m_clock.start();
    m_started = true;
    m_stopping = false;
    m_status.updatedAt = QDateTime::currentDateTimeUtc();
    QString serialError;
    if (!m_motionPort->start(serialDevice, baudRate, &serialError)) {
        m_controlPort->stop();
        m_started = false;
        if (error)
            *error = serialError;
        return false;
    }
    if (!serialError.isEmpty()) {
        m_status.detail = serialError;
        qWarning().noquote() << serialError;
    }
    m_refreshTimer.start();
    m_statusTimer.start();
    publishStatus();
    return true;
}

void MotionService::stop()
{
    if (!m_started)
        return;
    m_stopping = true;
    m_refreshTimer.stop();
    m_statusTimer.stop();
    endRemoteControl(QStringLiteral("Motion 服务停止"), false);
    if (m_controlPort)
        m_controlPort->stop();
    if (m_motionPort)
        m_motionPort->stop();
    m_started = false;
    m_stopping = false;
}

bool MotionService::isAvailable() const
{
    return m_started && m_controlPort;
}

FamilyMotionSession MotionService::createRemoteSession(QString* error)
{
    if (!isAvailable()) {
        if (error)
            *error = QStringLiteral("远程运动控制服务未启动");
        return {};
    }
    return m_controlPort->createSession(
        m_configuration.moveRefreshIntervalMs,
        m_configuration.remoteLeaseTimeoutMs,
        m_configuration.defaultSpeed,
        m_configuration.headStepUs, error);
}

MotionStatusSnapshot MotionService::status() const
{
    return m_status;
}

void MotionService::handleControllerStart(const QString& sessionId)
{
    if (sessionId.isEmpty() || !m_controlPort)
        return;
    if (!m_activeSessionId.isEmpty()) {
        m_controlPort->rejectController(
            sessionId, QStringLiteral("MOTION_CONTROL_BUSY"),
            QStringLiteral("已有家属正在远程控制设备"));
        return;
    }
    if (!m_status.uartAvailable) {
        m_controlPort->rejectController(
            sessionId, QStringLiteral("MOTION_UART_UNAVAILABLE"),
            QStringLiteral("LongPet 未连接 Motion MCU 串口"));
        return;
    }
    if (!isMcuFresh()) {
        m_controlPort->rejectController(
            sessionId, QStringLiteral("MOTION_MCU_OFFLINE"),
            QStringLiteral("Motion MCU 没有响应"));
        return;
    }
    if (m_status.fault) {
        m_controlPort->rejectController(
            sessionId, QStringLiteral("MOTION_MCU_FAULT"),
            QStringLiteral("Motion MCU 当前处于 fault 状态"));
        return;
    }

    QString commandError;
    if (!commandStop(&commandError)
        || !m_motionPort->sendMode(MotionControlMode::Manual, &commandError)
        || !m_motionPort->requestStatus(&commandError)) {
        m_controlPort->rejectController(
            sessionId, QStringLiteral("MOTION_COMMAND_FAILED"),
            commandError.isEmpty() ? QStringLiteral("无法进入 MANUAL 模式")
                                   : commandError);
        return;
    }
    m_activeSessionId = sessionId;
    m_status.remoteControlActive = true;
    m_status.mode = MotionControlMode::Manual;
    m_status.detail = QStringLiteral("远程操控已连接");
    m_lastRemoteRefreshMs = -1;
    m_lastMoveSentMs = -1;
    m_controlPort->acceptController(sessionId);
    publishStatus();
    qInfo().noquote() << "Motion remote session entered MANUAL" << sessionId;
}

void MotionService::handleControllerStopped(const QString& reason)
{
    if (!m_activeSessionId.isEmpty())
        endRemoteControl(reason, false);
}

void MotionService::handleChassisCommand(ChassisMotion motion, int speed)
{
    if (m_activeSessionId.isEmpty() || motion == ChassisMotion::Stopped
        || motion == ChassisMotion::Unknown || speed < 1 || speed > 100) {
        return;
    }
    const qint64 now = m_clock.elapsed();
    const bool changed = motion != m_requestedMotion || speed != m_requestedSpeed;
    m_requestedMotion = motion;
    m_requestedSpeed = speed;
    m_lastRemoteRefreshMs = now;
    if (!changed)
        return;

    QString error;
    if (!m_motionPort->sendMove(motion, speed, &error)) {
        endRemoteControl(error, true, QStringLiteral("MOTION_WRITE_FAILED"));
        return;
    }
    m_lastMoveSentMs = now;
    m_status.motion = motion;
    m_status.stopReason.clear();
    m_status.detail = QStringLiteral("远程底盘命令已发送");
    publishStatus();
}

void MotionService::handleStopRequested()
{
    if (m_activeSessionId.isEmpty())
        return;
    QString error;
    commandStop(&error);
    clearChassisState(error.isEmpty() ? QStringLiteral("家属端 STOP") : error);
    publishStatus();
}

void MotionService::handleHeadCommand(HeadMotion motion, int stepUs)
{
    if (m_activeSessionId.isEmpty())
        return;
    if (motion != HeadMotion::Center && (stepUs < 1 || stepUs > 100))
        return;
    QString error;
    if (!m_motionPort->sendHead(motion, stepUs, &error)) {
        endRemoteControl(error, true, QStringLiteral("MOTION_WRITE_FAILED"));
        return;
    }
    m_status.detail = motion == HeadMotion::Center
        ? QStringLiteral("头部已请求回中")
        : QStringLiteral("头部已请求向%1调整")
              .arg(motion == HeadMotion::Left ? QStringLiteral("左")
                                               : QStringLiteral("右"));
    publishStatus();
}

void MotionService::handleTransportAvailability(bool available,
                                                 const QString& detail)
{
    m_status.uartAvailable = available;
    m_status.detail = detail;
    m_status.updatedAt = QDateTime::currentDateTimeUtc();
    if (!available) {
        m_status.mcuOnline = false;
        m_lastMcuActivityMs = -1;
        if (!m_activeSessionId.isEmpty()) {
            endRemoteControl(QStringLiteral("Motion UART 连接已中断"), true,
                             QStringLiteral("MOTION_UART_DISCONNECTED"));
            return;
        }
    } else {
        QString error;
        commandStop(&error);
        m_motionPort->sendMode(MotionControlMode::Safe, &error);
        m_motionPort->requestStatus(&error);
    }
    publishStatus();
}

void MotionService::handleMcuActivity()
{
    m_lastMcuActivityMs = m_clock.isValid() ? m_clock.elapsed() : 0;
    if (!m_status.mcuOnline) {
        m_status.mcuOnline = true;
        m_status.detail = QStringLiteral("Motion MCU 已响应");
        publishStatus();
    }
}

void MotionService::handleMcuStatus(const MotionStatusSnapshot& status)
{
    const bool remoteActive = !m_activeSessionId.isEmpty();
    m_status = status;
    m_status.uartAvailable = m_motionPort && m_motionPort->isTransportAvailable();
    m_status.mcuOnline = true;
    m_status.remoteControlActive = remoteActive;
    m_status.detail = status.fault ? QStringLiteral("Motion MCU fault")
                                   : QStringLiteral("Motion MCU 状态正常");
    m_lastMcuActivityMs = m_clock.elapsed();
    if (status.fault && remoteActive) {
        endRemoteControl(QStringLiteral("Motion MCU 报告 fault"), true,
                         QStringLiteral("MOTION_MCU_FAULT"));
        return;
    }
    if (remoteActive && status.mode != MotionControlMode::Manual) {
        endRemoteControl(QStringLiteral("Motion MCU 已离开 MANUAL 模式"), true,
                         QStringLiteral("MOTION_MODE_CHANGED"));
        return;
    }
    publishStatus();
}

void MotionService::handleMcuFault(const QString& reason)
{
    m_status.fault = true;
    m_status.detail = QStringLiteral("Motion MCU fault：%1").arg(reason);
    if (!m_activeSessionId.isEmpty()) {
        endRemoteControl(m_status.detail, true,
                         QStringLiteral("MOTION_MCU_FAULT"));
        return;
    }
    publishStatus();
}

void MotionService::refreshMotionLease()
{
    if (!m_started)
        return;
    if (m_status.mcuOnline && !isMcuFresh()) {
        m_status.mcuOnline = false;
        m_status.detail = QStringLiteral("Motion MCU 状态响应超时");
        if (!m_activeSessionId.isEmpty()) {
            endRemoteControl(m_status.detail, true,
                             QStringLiteral("MOTION_MCU_OFFLINE"));
            return;
        }
        publishStatus();
    }
    if (m_activeSessionId.isEmpty()
        || m_requestedMotion == ChassisMotion::Stopped
        || m_lastRemoteRefreshMs < 0) {
        return;
    }
    const qint64 now = m_clock.elapsed();
    if (now - m_lastRemoteRefreshMs >= m_configuration.remoteLeaseTimeoutMs) {
        QString error;
        commandStop(&error);
        clearChassisState(QStringLiteral("远控指令租约超时，已停车"));
        publishStatus();
        return;
    }
    if (m_lastMoveSentMs >= 0
        && now - m_lastMoveSentMs < m_configuration.moveRefreshIntervalMs) {
        return;
    }
    QString error;
    if (!m_motionPort->sendMove(m_requestedMotion, m_requestedSpeed, &error)) {
        endRemoteControl(error, true, QStringLiteral("MOTION_WRITE_FAILED"));
        return;
    }
    m_lastMoveSentMs = now;
}

void MotionService::pollMcuStatus()
{
    if (!m_started || !m_motionPort || !m_motionPort->isTransportAvailable())
        return;
    QString error;
    if (!m_motionPort->requestStatus(&error) && !error.isEmpty())
        qWarning().noquote() << error;
}

void MotionService::publishStatus()
{
    m_status.remoteControlActive = !m_activeSessionId.isEmpty();
    m_status.updatedAt = QDateTime::currentDateTimeUtc();
    emit statusChanged(m_status);
    if (m_controlPort)
        m_controlPort->publishStatus(m_status);
}

bool MotionService::commandStop(QString* error)
{
    if (!m_motionPort || !m_motionPort->isTransportAvailable()) {
        if (error)
            *error = QStringLiteral("Motion UART 未连接");
        return false;
    }
    return m_motionPort->sendStop(error);
}

void MotionService::clearChassisState(const QString& reason)
{
    m_requestedMotion = ChassisMotion::Stopped;
    m_requestedSpeed = 0;
    m_lastRemoteRefreshMs = -1;
    m_lastMoveSentMs = -1;
    m_status.motion = ChassisMotion::Stopped;
    m_status.stopReason = reason;
    m_status.detail = reason;
}

void MotionService::endRemoteControl(const QString& reason, bool notifyClient,
                                     const QString& errorCode)
{
    const bool hadSession = !m_activeSessionId.isEmpty();
    m_activeSessionId.clear();
    QString commandError;
    commandStop(&commandError);
    clearChassisState(reason.isEmpty() ? QStringLiteral("远程控制已结束") : reason);
    if (m_motionPort && m_motionPort->isTransportAvailable())
        m_motionPort->sendMode(MotionControlMode::Safe, &commandError);
    m_status.mode = MotionControlMode::Safe;
    m_status.remoteControlActive = false;
    publishStatus();
    if (notifyClient && hadSession && m_controlPort) {
        m_controlPort->terminateController(
            errorCode.isEmpty() ? QStringLiteral("MOTION_CONTROL_ENDED") : errorCode,
            reason.isEmpty() ? QStringLiteral("远程运动控制已结束") : reason);
    }
    if (hadSession)
        qInfo().noquote() << "Motion remote session left MANUAL:" << reason;
}

bool MotionService::isMcuFresh() const
{
    return m_status.mcuOnline && m_lastMcuActivityMs >= 0 && m_clock.isValid()
        && m_clock.elapsed() - m_lastMcuActivityMs
            < m_configuration.mcuOfflineTimeoutMs;
}
