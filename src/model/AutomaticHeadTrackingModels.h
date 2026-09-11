#pragma once

#include "model/VisionModels.h"

#include <QDateTime>
#include <QString>

enum class AutomaticHeadTrackingState {
    Disabled,
    WaitingForVision,
    WaitingForMotion,
    Searching,
    Tracking,
    ManualOverride,
    VideoCallSuspended,
    Fault
};

struct AutomaticHeadTrackingSnapshot {
    bool enabled = false;
    bool active = false;
    AutomaticHeadTrackingState state = AutomaticHeadTrackingState::Disabled;
    TargetTrackingStatus visionStatus = TargetTrackingStatus::Searching;
    quint64 frameSequence = 0;
    qint64 targetAgeMs = -1;
    int dx = 0;
    int dy = 0;
    int area = 0;
    QString detail;
    QDateTime updatedAt;
};

QString automaticHeadTrackingStateName(AutomaticHeadTrackingState state);

Q_DECLARE_METATYPE(AutomaticHeadTrackingSnapshot)
