#pragma once

#include "model/VisionModels.h"

#include <QDateTime>
#include <QString>

struct FamilyVisionSession {
    QString sessionId;
    QString token;
    quint16 port = 0;
    int protocolVersion = 1;
    int frameRate = 0;
    QDateTime expiresAt;

    bool isValid() const
    {
        return !sessionId.isEmpty() && !token.isEmpty() && port > 0;
    }
};

struct FamilyVisionTelemetry {
    TargetObservation observation;
    QString detectorName;
    QString trackerName;
    double targetUpdateHz = 0.0;
};
