#pragma once

#include <QString>

struct UserSettings {
    int volume = 60;
    int brightness = 72;
    QString petStyle = QStringLiteral("温和陪伴");
    //新加的
    QString temperatureUnit = QStringLiteral("celsius");
    //
};

struct DeviceSummary {
    QString softwareVersion;
    QString networkSummary;
    QString familySummary;
    bool audioControlAvailable = false;
    QString audioSummary;
    bool brightnessControlAvailable = false;
    int brightnessLevels = 0;
    QString brightnessSummary;
    QString powerSummary;
    bool keywordSpottingAvailable = false;
    bool keywordSpottingListening = false;
    QString keywordSpottingSummary;
    QString lastKeyword;
    bool visionAvailable = false;
    bool visionMonitoring = false;
    QString visionSummary;
    double visionFps = 0.0;
};
