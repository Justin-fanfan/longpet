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

struct VisionConfig {
    bool enabled = false;
    int cameraIndex = 0;
    int frameWidth = 320;
    int frameHeight = 240;
    int targetFps = 5;
    bool waveEnabled = true;
    bool fallCandidateEnabled = false;
};

struct VisionStatus {
    VisionRuntimeState state = VisionRuntimeState::Disabled;
    bool enabled = false;
    bool available = false;
    bool running = false;
    bool cameraAvailable = false;
    bool monitoring = false;
    QString summary = QStringLiteral("视觉感知未启动");
    double effectiveFps = 0.0;
    double frameTimeMs = 0.0;
    int cameraIndex = -1;
    VisionEventType lastEventType = VisionEventType::Unknown;
    QDateTime lastEventAt;
    double lastConfidence = 0.0;
    qint64 workerPid = 0;
    QDateTime startedAt;
    QString errorDetail;
    int frameWidth = 320;
    int frameHeight = 240;
    int targetFps = 5;
    bool waveEnabled = true;
    bool fallCandidateEnabled = false;
};

Q_DECLARE_METATYPE(VisionDetection)
Q_DECLARE_METATYPE(VisionConfig)
Q_DECLARE_METATYPE(VisionEventType)
Q_DECLARE_METATYPE(VisionRuntimeState)
Q_DECLARE_METATYPE(VisionStatus)
