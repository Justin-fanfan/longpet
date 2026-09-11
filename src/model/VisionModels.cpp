#include "VisionModels.h"

#include <algorithm>

namespace {
qreal clampUnit(qreal value)
{
    return std::clamp(value, 0.0, 1.0);
}
}

PersonDetection VisionGeometry::personFromNormalizedRect(
    float confidence, const QRectF& normalizedRect, const QSize& sourceSize)
{
    const qreal left = clampUnit(normalizedRect.left());
    const qreal top = clampUnit(normalizedRect.top());
    const qreal right = clampUnit(normalizedRect.right());
    const qreal bottom = clampUnit(normalizedRect.bottom());
    const QRectF clipped(QPointF(std::min(left, right), std::min(top, bottom)),
                         QPointF(std::max(left, right), std::max(top, bottom)));

    PersonDetection detection;
    detection.confidence = confidence;
    detection.normalizedCenter = clipped.center();
    detection.normalizedSize = clipped.size();
    detection.boundingBox = QRectF(
        clipped.x() * sourceSize.width(),
        clipped.y() * sourceSize.height(),
        clipped.width() * sourceSize.width(),
        clipped.height() * sourceSize.height());
    return detection;
}

QString targetTrackingStatusName(TargetTrackingStatus status)
{
    switch (status) {
    case TargetTrackingStatus::Searching:
        return QStringLiteral("SEARCHING");
    case TargetTrackingStatus::Detected:
        return QStringLiteral("DETECTED");
    case TargetTrackingStatus::Tracking:
        return QStringLiteral("TRACKING");
    case TargetTrackingStatus::Corrected:
        return QStringLiteral("CORRECTED");
    case TargetTrackingStatus::Lost:
        return QStringLiteral("LOST");
    case TargetTrackingStatus::Reacquired:
        return QStringLiteral("REACQUIRED");
    }
    return QStringLiteral("SEARCHING");
}
