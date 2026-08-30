#pragma once

#include <QDateTime>
#include <QString>

struct SystemStatus {
    QDateTime currentDateTime;
    QString weatherSummary = QStringLiteral("--");
    // 和风 icon 编码（如 "100"），用于状态栏天气图标；没有数据时为空。
    QString weatherConditionCode;
    bool networkKnown = false;
    bool networkAvailable = false;
    QString networkSummary;
    int batteryPercent = -1;
};
