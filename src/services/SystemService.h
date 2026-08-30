#pragma once

#include "model/SettingsModels.h"
#include "model/SystemModels.h"

#include <QObject>
#include <QTimer>

class SystemService final : public QObject {
    Q_OBJECT

public:
    explicit SystemService(QObject* parent = nullptr);

    SystemStatus status() const;
    DeviceSummary deviceSummary() const;
    void setNetworkState(bool known, bool available, const QString& summary = {});
    void setBatteryPercent(int percent);
    // conditionCode 是和风 icon 编码（"100" 等），用于状态栏天气图标；可为空。
    void setWeatherSummary(const QString& summary, const QString& conditionCode = {});
    void setAudioControlState(bool available, const QString& summary);
    void setBacklightControlState(bool available, int levels, const QString& summary);
    void setPowerSummary(const QString& summary);

signals:
    void statusChanged(const SystemStatus& status);
    void deviceSummaryChanged(const DeviceSummary& summary);

private:
    void refreshClock();

    SystemStatus m_status;
    DeviceSummary m_deviceSummary;
    QTimer m_clockTimer;
};
