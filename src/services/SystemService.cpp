#include "SystemService.h"

#include <QCoreApplication>

SystemService::SystemService(QObject* parent)
    : QObject(parent)
{
    m_clockTimer.setInterval(30'000);
    m_clockTimer.setTimerType(Qt::VeryCoarseTimer);
    connect(&m_clockTimer, &QTimer::timeout,
            this, &SystemService::refreshClock);
    refreshClock();
    m_clockTimer.start();
}

SystemStatus SystemService::status() const
{
    return m_status;
}

DeviceSummary SystemService::deviceSummary() const
{
    DeviceSummary summary;
    summary.softwareVersion = QCoreApplication::applicationVersion();
    summary.networkSummary = m_status.networkSummary.isEmpty()
        ? QStringLiteral("网络状态未知") : m_status.networkSummary;
    summary.familySummary = QStringLiteral("尚未配对");
    return summary;
}

void SystemService::setNetworkState(bool known, bool available, const QString& summary)
{
    m_status.networkKnown = known;
    m_status.networkAvailable = known && available;
    m_status.networkSummary = summary.simplified();
    if (m_status.networkSummary.isEmpty()) {
        m_status.networkSummary = known
            ? (available ? QStringLiteral("已联网") : QStringLiteral("未连接"))
            : QStringLiteral("网络状态未知");
    }
    emit statusChanged(m_status);
    emit deviceSummaryChanged(deviceSummary());
}

void SystemService::setBatteryPercent(int percent)
{
    m_status.batteryPercent = percent < 0 ? -1 : qBound(0, percent, 100);
    emit statusChanged(m_status);
}

void SystemService::setWeatherSummary(const QString& summary)
{
    m_status.weatherSummary = summary.simplified().isEmpty()
        ? QStringLiteral("--") : summary.simplified();
    emit statusChanged(m_status);
}

void SystemService::refreshClock()
{
    m_status.currentDateTime = QDateTime::currentDateTime();
    emit statusChanged(m_status);
}
