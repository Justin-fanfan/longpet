#pragma once

#include <QDateTime>
#include <QString>

enum class MotionControlMode {
    Unknown,
    Safe,
    HeadOnly,
    Manual,
    Follow
};

enum class ChassisMotion {
    Unknown,
    Stopped,
    Forward,
    Backward,
    RotateLeft,
    RotateRight
};

enum class HeadMotion {
    Left,
    Center,
    Right
};

struct MotionStatusSnapshot {
    bool uartAvailable = false;
    bool mcuOnline = false;
    bool fault = false;
    bool remoteControlActive = false;
    bool automaticHeadTrackingActive = false;
    MotionControlMode mode = MotionControlMode::Unknown;
    ChassisMotion motion = ChassisMotion::Stopped;
    QString stopReason;
    int servoPulseUs = -1;
    bool targetAvailable = false;
    bool imuAvailable = false;
    QString detail;
    QDateTime updatedAt;
};

struct MotionTargetFrame {
    int dx = 0;
    int dy = 0;
    int area = 0;

    bool isLost() const { return area == 0; }
};

struct FamilyMotionSession {
    QString sessionId;
    QString token;
    quint16 port = 0;
    int protocolVersion = 1;
    int refreshIntervalMs = 150;
    int leaseTimeoutMs = 350;
    int defaultSpeed = 20;
    int headStepUs = 20;
    QDateTime expiresAt;

    bool isValid() const
    {
        return !sessionId.isEmpty() && !token.isEmpty() && port > 0;
    }
};

QString motionControlModeName(MotionControlMode mode);
QString chassisMotionName(ChassisMotion motion);
QString headMotionName(HeadMotion motion);
bool motionControlModeFromName(const QString& name, MotionControlMode* mode);
bool chassisMotionFromName(const QString& name, ChassisMotion* motion);
bool headMotionFromName(const QString& name, HeadMotion* motion);

Q_DECLARE_METATYPE(MotionStatusSnapshot)
Q_DECLARE_METATYPE(ChassisMotion)
Q_DECLARE_METATYPE(HeadMotion)
Q_DECLARE_METATYPE(MotionTargetFrame)
