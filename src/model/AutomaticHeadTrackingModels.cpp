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
