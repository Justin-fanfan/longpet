#include "EspMotionProtocol.h"

#include <QRegularExpression>

QByteArray EspMotionProtocol::stopCommand()
{
    return QByteArrayLiteral("STOP\n");
}

QByteArray EspMotionProtocol::modeCommand(MotionControlMode mode)
{
    if (mode == MotionControlMode::Unknown)
        return {};
    return QByteArrayLiteral("MODE ") + motionControlModeName(mode).toLatin1()
        + '\n';
}

QByteArray EspMotionProtocol::moveCommand(ChassisMotion motion, int speed)
{
    if (motion == ChassisMotion::Unknown || motion == ChassisMotion::Stopped
        || speed < 1 || speed > 100) {
        return {};
    }
    return QByteArrayLiteral("MOVE ") + chassisMotionName(motion).toLatin1()
        + ' ' + QByteArray::number(speed) + '\n';
}

QByteArray EspMotionProtocol::headCommand(HeadMotion motion, int stepUs)
{
    if (motion == HeadMotion::Center)
        return QByteArrayLiteral("HEAD CENTER\n");
    if (stepUs < 1 || stepUs > 100)
        return {};
    return QByteArrayLiteral("HEAD ") + headMotionName(motion).toLatin1()
        + ' ' + QByteArray::number(stepUs) + '\n';
}

QByteArray EspMotionProtocol::targetCommand(const MotionTargetFrame& target)
{
    if (target.dx < -4'096 || target.dx > 4'096
        || target.dy < -4'096 || target.dy > 4'096
        || target.area < 0 || target.area > 16'777'216) {
        return {};
    }
    return QByteArrayLiteral("TARGET ") + QByteArray::number(target.dx)
        + ' ' + QByteArray::number(target.dy)
        + ' ' + QByteArray::number(target.area) + '\n';
}

QByteArray EspMotionProtocol::statusCommand()
{
    return QByteArrayLiteral("STATUS\n");
}

bool EspMotionProtocol::parseStatusLine(const QByteArray& line,
                                        MotionStatusSnapshot* status,
                                        QString* error)
{
    if (!status) {
        if (error)
            *error = QStringLiteral("状态输出不能为空");
        return false;
    }
    static const QRegularExpression pattern(QStringLiteral(
        R"(^\[STATUS\] mode=([A-Z_]+) motion=([A-Z_]+) stop=([A-Z_]+) fault=([01]) target=([01]) servo=([0-9]+) imu=([01])$)"));
    const QString text = QString::fromLatin1(line).trimmed();
    const QRegularExpressionMatch match = pattern.match(text);
    if (!match.hasMatch()) {
        if (error)
            *error = QStringLiteral("MCU STATUS 格式无效");
        return false;
    }
    MotionControlMode mode = MotionControlMode::Unknown;
    ChassisMotion motion = ChassisMotion::Unknown;
    if (!motionControlModeFromName(match.captured(1), &mode)
        || !chassisMotionFromName(match.captured(2), &motion)) {
        if (error)
            *error = QStringLiteral("MCU STATUS 枚举值无效");
        return false;
    }
    status->mode = mode;
    status->motion = motion;
    status->stopReason = match.captured(3);
    status->fault = match.captured(4) == QStringLiteral("1");
    status->targetAvailable = match.captured(5) == QStringLiteral("1");
    status->servoPulseUs = match.captured(6).toInt();
    status->imuAvailable = match.captured(7) == QStringLiteral("1");
    status->updatedAt = QDateTime::currentDateTimeUtc();
    return true;
}
