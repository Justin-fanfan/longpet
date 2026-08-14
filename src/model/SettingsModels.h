#pragma once

#include <QString>

struct UserSettings {
    int volume = 60;
    int brightness = 72;
    QString petStyle = QStringLiteral("温和陪伴");
};

struct DeviceSummary {
    QString softwareVersion;
    QString networkSummary;
    QString familySummary;
};

