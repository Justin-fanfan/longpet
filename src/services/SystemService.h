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
    void setWeatherSummary(const QString& summary);
    void setAudioControlState(bool available, const QString& summary);
    void setBacklightControlState(bool available, int levels, const QString& summary);
    void setPowerSummary(const QString& summary);
    void setKeywordSpottingState(bool available, bool listening,
                                 const QString& summary,
                                 const QString& lastKeyword = {});

signals:
    void statusChanged(const SystemStatus& status);
    void deviceSummaryChanged(const DeviceSummary& summary);

private:
    void refreshClock();

    SystemStatus m_status;
    DeviceSummary m_deviceSummary;
    QTimer m_clockTimer;
};
