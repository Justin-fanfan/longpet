#include "FamilyVisionProtocol.h"

#include <QJsonDocument>

#include <algorithm>

namespace {
double clampUnit(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

QString isoDateTime(const QDateTime& value)
{
    return value.isValid() ? value.toUTC().toString(Qt::ISODateWithMs) : QString();
}
}

QJsonObject FamilyVisionProtocol::telemetryObject(
    const FamilyVisionTelemetry& telemetry)
{
    const TargetObservation& observation = telemetry.observation;
    const bool drawable = observation.present && observation.fresh
        && observation.status != TargetTrackingStatus::Searching
        && observation.status != TargetTrackingStatus::Lost;

    QJsonObject object {
        {QStringLiteral("type"), QStringLiteral("vision_target")},
        {QStringLiteral("protocol_version"), Version},
        {QStringLiteral("frame_sequence"),
         static_cast<double>(observation.frameSequence)},
        {QStringLiteral("capture_timestamp"), isoDateTime(observation.timestamp)},
        {QStringLiteral("published_at"), isoDateTime(observation.publishedAt)},
        {QStringLiteral("present"), observation.present},
        {QStringLiteral("fresh"), observation.fresh},
        {QStringLiteral("state"), targetTrackingStatusName(observation.status)},
        {QStringLiteral("age_ms"), static_cast<double>(observation.ageMs)},
        {QStringLiteral("detector_confidence"), observation.detectorConfidence},
        {QStringLiteral("tracker_confidence"), observation.trackerConfidence},
        {QStringLiteral("tracked_points"), observation.trackedPointCount},
        {QStringLiteral("detector_ran"), observation.detectorRan},
        {QStringLiteral("detector_ms"), observation.detectorMs},
        {QStringLiteral("tracker_ms"), observation.trackerMs},
        {QStringLiteral("target_update_hz"), telemetry.targetUpdateHz},
        {QStringLiteral("detector"), telemetry.detectorName},
        {QStringLiteral("tracker"), telemetry.trackerName},
        {QStringLiteral("diagnostic"), observation.diagnostic}
    };

    if (drawable) {
        const QSizeF size = observation.target.normalizedSize;
        const QPointF center = observation.target.normalizedCenter;
        const double x = clampUnit(center.x() - size.width() / 2.0);
        const double y = clampUnit(center.y() - size.height() / 2.0);
        const double right = clampUnit(center.x() + size.width() / 2.0);
        const double bottom = clampUnit(center.y() + size.height() / 2.0);
        object.insert(QStringLiteral("bbox"), QJsonObject {
            {QStringLiteral("x"), x},
            {QStringLiteral("y"), y},
            {QStringLiteral("w"), std::max(0.0, right - x)},
            {QStringLiteral("h"), std::max(0.0, bottom - y)}
        });
    } else {
        object.insert(QStringLiteral("bbox"), QJsonValue::Null);
    }
    return object;
}

QByteArray FamilyVisionProtocol::telemetryPayload(
    const FamilyVisionTelemetry& telemetry)
{
    return QJsonDocument(telemetryObject(telemetry))
        .toJson(QJsonDocument::Compact);
}
