#pragma once

#include <QDateTime>
#include <QString>

struct SystemStatus {
    QDateTime currentDateTime;
    QString weatherSummary = QStringLiteral("--");
    bool networkKnown = false;
    bool networkAvailable = false;
    int batteryPercent = -1;
};

