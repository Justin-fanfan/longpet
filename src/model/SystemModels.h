#pragma once

#include <QDateTime>
#include <QString>

struct SystemStatus {
    QDateTime currentDateTime;
    QString weatherSummary = QStringLiteral("--");
    bool networkKnown = false;
    bool networkAvailable = false;
    QString networkSummary;
    int batteryPercent = -1;
};
