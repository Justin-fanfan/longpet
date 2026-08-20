#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QMetaType>
#include <QString>

enum class VisionEventType {
    Unknown,
    PersonDetected,
    FallCandidate,
    FallConfirmed,
    Wave
};

enum class VisionRuntimeState {
    Disabled,
    Starting,
    Running,
    Degraded,
    Error
};

struct VisionDetection {
    VisionEventType type = VisionEventType::Unknown;
    double confidence = 0.0;
    QDateTime timestamp;
    QString source;
    QString trackId;
    QJsonObject metadata;
};

struct VisionStatus {
    VisionRuntimeState state = VisionRuntimeState::Disabled;
    bool available = false;
    bool cameraAvailable = false;
    bool monitoring = false;
    QString summary = QStringLiteral("视觉感知未启动");
    double effectiveFps = 0.0;
    double frameTimeMs = 0.0;
    int cameraIndex = -1;
    VisionEventType lastEventType = VisionEventType::Unknown;
    QDateTime lastEventAt;
};

Q_DECLARE_METATYPE(VisionDetection)
Q_DECLARE_METATYPE(VisionEventType)
Q_DECLARE_METATYPE(VisionRuntimeState)
Q_DECLARE_METATYPE(VisionStatus)
