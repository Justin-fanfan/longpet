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

signals:
    void statusChanged(const SystemStatus& status);
    void deviceSummaryChanged(const DeviceSummary& summary);

private:
    void refreshClock();

    SystemStatus m_status;
    QString m_networkSummary;
    QTimer m_clockTimer;
};

