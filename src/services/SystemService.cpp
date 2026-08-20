#include "SystemService.h"

#include <QCoreApplication>

SystemService::SystemService(QObject* parent)
    : QObject(parent)
{
    m_deviceSummary.softwareVersion = QCoreApplication::applicationVersion();
    m_deviceSummary.networkSummary = QStringLiteral("网络状态未知");
    m_deviceSummary.familySummary = QStringLiteral("尚未配对");
    m_deviceSummary.audioSummary = QStringLiteral("未检测到音量控制");
    m_deviceSummary.brightnessSummary = QStringLiteral("未检测到背光控制");
    m_deviceSummary.powerSummary = QStringLiteral("电源状态未知");
    m_deviceSummary.keywordSpottingSummary = QStringLiteral("关键词识别未启动");
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
    DeviceSummary summary = m_deviceSummary;
    summary.softwareVersion = QCoreApplication::applicationVersion();
    summary.networkSummary = m_status.networkSummary.isEmpty()
        ? QStringLiteral("网络状态未知") : m_status.networkSummary;
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

void SystemService::setAudioControlState(bool available, const QString& summary)
{
    m_deviceSummary.audioControlAvailable = available;
    m_deviceSummary.audioSummary = summary.simplified().isEmpty()
        ? (available ? QStringLiteral("音量控制已接入")
                     : QStringLiteral("未检测到音量控制"))
        : summary.simplified();
    emit deviceSummaryChanged(deviceSummary());
}

void SystemService::setBacklightControlState(bool available, int levels,
                                             const QString& summary)
{
    m_deviceSummary.brightnessControlAvailable = available;
    m_deviceSummary.brightnessLevels = available ? qMax(0, levels) : 0;
    m_deviceSummary.brightnessSummary = summary.simplified().isEmpty()
        ? (available ? QStringLiteral("背光控制已接入")
                     : QStringLiteral("未检测到背光控制"))
        : summary.simplified();
    emit deviceSummaryChanged(deviceSummary());
}

void SystemService::setPowerSummary(const QString& summary)
{
    m_deviceSummary.powerSummary = summary.simplified().isEmpty()
        ? QStringLiteral("电源状态未知") : summary.simplified();
    emit deviceSummaryChanged(deviceSummary());
}

void SystemService::setKeywordSpottingState(bool available, bool listening,
                                            const QString& summary,
                                            const QString& lastKeyword)
{
    m_deviceSummary.keywordSpottingAvailable = available;
    m_deviceSummary.keywordSpottingListening = available && listening;
    m_deviceSummary.keywordSpottingSummary = summary.simplified().isEmpty()
        ? (listening ? QStringLiteral("离线关键词 · 正在监听")
                     : QStringLiteral("关键词识别不可用"))
        : summary.simplified();
    m_deviceSummary.lastKeyword = lastKeyword.simplified();
    emit deviceSummaryChanged(deviceSummary());
}

void SystemService::refreshClock()
{
    m_status.currentDateTime = QDateTime::currentDateTime();
    emit statusChanged(m_status);
}
