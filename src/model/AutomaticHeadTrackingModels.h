#pragma once

#include "model/VisionModels.h"
#include "model/MotionModels.h"

#include <QDateTime>
#include <QString>

enum class AutomaticTrackingMode {
    Disabled,
    HeadOnly,
    PersonFollow
};

enum class PersonFollowState {
    Disabled,
    Acquiring,
    Aligning,
    Approaching,
    Holding,
    Lost
};

enum class PersonDistanceClass {
    Unknown,
    Far,
    Good,
    Near
};

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
    AutomaticTrackingMode mode = AutomaticTrackingMode::Disabled;
    AutomaticHeadTrackingState state = AutomaticHeadTrackingState::Disabled;
    PersonFollowState followState = PersonFollowState::Disabled;
    PersonDistanceClass distanceClass = PersonDistanceClass::Unknown;
    PhysicalHeadDirection headDirection = PhysicalHeadDirection::Unknown;
    TargetTrackingStatus visionStatus = TargetTrackingStatus::Searching;
    quint64 frameSequence = 0;
    qint64 targetAgeMs = -1;
    int dx = 0;
    int dy = 0;
    int area = 0;
    qreal normalizedBboxWidth = 0.0;
    qreal normalizedBboxHeight = 0.0;
    qreal normalizedBboxAreaRatio = 0.0;
    bool headOffsetAvailable = false;
    int headOffsetUs = 0;
    ChassisMotion chassisMotion = ChassisMotion::Stopped;
    qint64 targetStableMs = 0;
    QString detail;
    QDateTime updatedAt;
};

QString automaticHeadTrackingStateName(AutomaticHeadTrackingState state);
QString automaticTrackingModeName(AutomaticTrackingMode mode);
bool automaticTrackingModeFromName(const QString& name,
                                   AutomaticTrackingMode* mode);
QString personFollowStateName(PersonFollowState state);
QString personDistanceClassName(PersonDistanceClass distanceClass);

Q_DECLARE_METATYPE(AutomaticHeadTrackingSnapshot)
Q_DECLARE_METATYPE(AutomaticTrackingMode)
