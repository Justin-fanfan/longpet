#include "AutomaticHeadTrackingModels.h"

QString automaticHeadTrackingStateName(AutomaticHeadTrackingState state)
{
    switch (state) {
    case AutomaticHeadTrackingState::Disabled:
        return QStringLiteral("DISABLED");
    case AutomaticHeadTrackingState::WaitingForVision:
        return QStringLiteral("WAITING_FOR_VISION");
    case AutomaticHeadTrackingState::WaitingForMotion:
        return QStringLiteral("WAITING_FOR_MOTION");
    case AutomaticHeadTrackingState::Searching:
        return QStringLiteral("SEARCHING");
    case AutomaticHeadTrackingState::Tracking:
        return QStringLiteral("TRACKING");
    case AutomaticHeadTrackingState::ManualOverride:
        return QStringLiteral("MANUAL_OVERRIDE");
    case AutomaticHeadTrackingState::VideoCallSuspended:
        return QStringLiteral("VIDEO_CALL_SUSPENDED");
    case AutomaticHeadTrackingState::Fault:
        return QStringLiteral("FAULT");
    }
    return QStringLiteral("DISABLED");
}

QString automaticTrackingModeName(AutomaticTrackingMode mode)
{
    switch (mode) {
    case AutomaticTrackingMode::Disabled: return QStringLiteral("DISABLED");
    case AutomaticTrackingMode::HeadOnly: return QStringLiteral("HEAD_ONLY");
    case AutomaticTrackingMode::PersonFollow:
        return QStringLiteral("PERSON_FOLLOW");
    }
    return QStringLiteral("DISABLED");
}

bool automaticTrackingModeFromName(const QString& name,
                                   AutomaticTrackingMode* mode)
{
    if (!mode)
        return false;
    const QString value = name.trimmed().toUpper();
    if (value == QStringLiteral("DISABLED"))
        *mode = AutomaticTrackingMode::Disabled;
    else if (value == QStringLiteral("HEAD_ONLY"))
        *mode = AutomaticTrackingMode::HeadOnly;
    else if (value == QStringLiteral("PERSON_FOLLOW"))
        *mode = AutomaticTrackingMode::PersonFollow;
    else
        return false;
    return true;
}

QString personFollowStateName(PersonFollowState state)
{
    switch (state) {
    case PersonFollowState::Disabled: return QStringLiteral("DISABLED");
    case PersonFollowState::Acquiring: return QStringLiteral("ACQUIRING");
    case PersonFollowState::Aligning: return QStringLiteral("ALIGNING");
    case PersonFollowState::Approaching: return QStringLiteral("APPROACHING");
    case PersonFollowState::Holding: return QStringLiteral("HOLDING");
    case PersonFollowState::Lost: return QStringLiteral("LOST");
    }
    return QStringLiteral("DISABLED");
}

QString personDistanceClassName(PersonDistanceClass distanceClass)
{
    switch (distanceClass) {
    case PersonDistanceClass::Unknown: return QStringLiteral("UNKNOWN");
    case PersonDistanceClass::Far: return QStringLiteral("FAR");
    case PersonDistanceClass::Good: return QStringLiteral("GOOD");
    case PersonDistanceClass::Near: return QStringLiteral("NEAR");
    }
    return QStringLiteral("UNKNOWN");
}
