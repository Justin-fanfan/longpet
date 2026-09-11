#pragma once

#include "model/FamilyVisionModels.h"

#include <QByteArray>
#include <QJsonObject>

namespace FamilyVisionProtocol {
constexpr int Version = 1;

QJsonObject telemetryObject(const FamilyVisionTelemetry& telemetry);
QByteArray telemetryPayload(const FamilyVisionTelemetry& telemetry);
}
