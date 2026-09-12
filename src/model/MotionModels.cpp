#include "MotionModels.h"

QString motionControlModeName(MotionControlMode mode)
{
    switch (mode) {
    case MotionControlMode::Safe: return QStringLiteral("SAFE");
    case MotionControlMode::HeadOnly: return QStringLiteral("HEAD_ONLY");
    case MotionControlMode::Manual: return QStringLiteral("MANUAL");
    case MotionControlMode::Follow: return QStringLiteral("FOLLOW");
    case MotionControlMode::Unknown: break;
    }
    return QStringLiteral("UNKNOWN");
}

QString chassisMotionName(ChassisMotion motion)
{
    switch (motion) {
    case ChassisMotion::Stopped: return QStringLiteral("STOPPED");
    case ChassisMotion::Forward: return QStringLiteral("FORWARD");
    case ChassisMotion::Backward: return QStringLiteral("BACKWARD");
    case ChassisMotion::RotateLeft: return QStringLiteral("ROTATE_LEFT");
    case ChassisMotion::RotateRight: return QStringLiteral("ROTATE_RIGHT");
    case ChassisMotion::Unknown: break;
    }
    return QStringLiteral("UNKNOWN");
}

QString headMotionName(HeadMotion motion)
{
    switch (motion) {
    case HeadMotion::Left: return QStringLiteral("LEFT");
    case HeadMotion::Center: return QStringLiteral("CENTER");
    case HeadMotion::Right: return QStringLiteral("RIGHT");
    }
    return QStringLiteral("CENTER");
}

QString physicalHeadDirectionName(PhysicalHeadDirection direction)
{
    switch (direction) {
    case PhysicalHeadDirection::Left: return QStringLiteral("LEFT");
    case PhysicalHeadDirection::Centered: return QStringLiteral("CENTERED");
    case PhysicalHeadDirection::Right: return QStringLiteral("RIGHT");
    case PhysicalHeadDirection::Unknown: break;
    }
    return QStringLiteral("UNKNOWN");
}

PhysicalHeadDirection physicalHeadDirection(int offsetUs, int centeredUs)
{
    if (centeredUs < 0)
        return PhysicalHeadDirection::Unknown;
    if (offsetUs < -centeredUs)
        return PhysicalHeadDirection::Left;
    if (offsetUs > centeredUs)
        return PhysicalHeadDirection::Right;
    return PhysicalHeadDirection::Centered;
}

bool motionControlModeFromName(const QString& name, MotionControlMode* mode)
{
    if (!mode)
        return false;
    const QString value = name.trimmed().toUpper();
    if (value == QStringLiteral("SAFE")) *mode = MotionControlMode::Safe;
    else if (value == QStringLiteral("HEAD_ONLY")) *mode = MotionControlMode::HeadOnly;
    else if (value == QStringLiteral("MANUAL")) *mode = MotionControlMode::Manual;
    else if (value == QStringLiteral("FOLLOW")) *mode = MotionControlMode::Follow;
    else return false;
    return true;
}

bool chassisMotionFromName(const QString& name, ChassisMotion* motion)
{
    if (!motion)
        return false;
    const QString value = name.trimmed().toUpper();
    if (value == QStringLiteral("STOPPED")) *motion = ChassisMotion::Stopped;
    else if (value == QStringLiteral("FORWARD")) *motion = ChassisMotion::Forward;
    else if (value == QStringLiteral("BACKWARD")) *motion = ChassisMotion::Backward;
    else if (value == QStringLiteral("ROTATE_LEFT")) *motion = ChassisMotion::RotateLeft;
    else if (value == QStringLiteral("ROTATE_RIGHT")) *motion = ChassisMotion::RotateRight;
    else return false;
    return true;
}

bool headMotionFromName(const QString& name, HeadMotion* motion)
{
    if (!motion)
        return false;
    const QString value = name.trimmed().toUpper();
    if (value == QStringLiteral("LEFT")) *motion = HeadMotion::Left;
    else if (value == QStringLiteral("CENTER")) *motion = HeadMotion::Center;
    else if (value == QStringLiteral("RIGHT")) *motion = HeadMotion::Right;
    else return false;
    return true;
}
