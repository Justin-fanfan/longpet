#include "services/WeatherService.h"

#include "services/SystemService.h"
#include "services/WeatherPorts.h"

#include <QDateTime>
#include <QDebug>

#include <utility>

WeatherService::WeatherService(WeatherConfiguration configuration,
                               WeatherProviderPort* provider,
                               SystemService* systemService,
                               QObject* parent)
    : QObject(parent),
      m_configuration(std::move(configuration)),
      m_provider(provider),
      m_systemService(systemService)
{
    m_refreshTimer.setTimerType(Qt::VeryCoarseTimer);
    connect(&m_refreshTimer, &QTimer::timeout, this, [this] { refresh(); });

    // 无数据的短间隔重试：网络一恢复（或首次拉取因启动竞态失败）后，
    // 无需干等 refresh_minutes 就能拿到第一条天气。
    m_retryTimer.setSingleShot(true);
    m_retryTimer.setInterval(60'000);
    m_retryTimer.setTimerType(Qt::VeryCoarseTimer);
    connect(&m_retryTimer, &QTimer::timeout, this, [this] { refresh(); });

    // 网络恢复上升沿 → 立即补刷（见头文件"启动竞态"说明）。
    if (m_systemService) {
        connect(m_systemService, &SystemService::statusChanged,
                this, &WeatherService::handleSystemStatus);
    }
}

WeatherService::~WeatherService()
{
    stop();
}

void WeatherService::start()
{
    if (m_started)
        return;
    if (!m_configuration.enabled) {
        qInfo().noquote() << "Weather disabled; weather service not started";
        return;
    }
    if (!m_provider) {
        qWarning().noquote() << "No weather provider configured; service not started";
        return;
    }

    qInfo().noquote()
        << "Weather service starting, provider=" << m_configuration.provider
        << "refresh_minutes=" << m_configuration.refreshMinutes;
    if (!m_connected && m_provider) {
        connect(m_provider, &WeatherProviderPort::currentWeatherReady,
                this, &WeatherService::handleCurrentWeather);
        connect(m_provider, &WeatherProviderPort::fetchFailed,
                this, &WeatherService::handleFetchFailed);
        m_connected = true;
    }
    m_started = true;
    scheduleNextRefresh();
    // 启动后立即刷一次；若离线则由 refresh() 判断后跳过。
    refresh();
}

void WeatherService::stop()
{
    if (!m_started && !m_connected)
        return;
    m_refreshTimer.stop();
    m_retryTimer.stop();
    m_started = false;
    m_inFlight = false;
    if (m_provider) {
        // 先断开再取消，确保 stop 之后 provider 的迟到回调不再更新状态。
        disconnect(m_provider, nullptr, this, nullptr);
        m_connected = false;
        m_provider->cancel();
    }
}

bool WeatherService::isActive() const
{
    return m_started;
}

WeatherSnapshot WeatherService::current() const
{
    return currentOrNone().value_or(WeatherSnapshot{});
}

std::optional<WeatherSnapshot> WeatherService::currentOrNone() const
{
    if (!m_last || !m_last->valid)
        return std::nullopt;
    WeatherSnapshot snapshot = *m_last;
    snapshot.stale = isStale(snapshot.updatedAt);
    return snapshot;
}

QString WeatherService::currentSummary() const
{
    const auto snapshot = currentOrNone();
    return snapshot ? snapshot->summary() : QString();
}

void WeatherService::refresh()
{
    if (!m_started || m_inFlight)
        return;
    if (!m_configuration.enabled)
        return;
    if (!m_systemService || !m_systemService->status().networkAvailable) {
        // 离线：不发请求、不清除已有数据。
        qInfo().noquote() << "Weather refresh skipped: network unavailable";
        if (m_last) {
            // 已有数据时按 refresh_minutes 正常节奏即可。
            return;
        }
        // 连续多次判定离线仍无数据：强行探测一次，防止网络状态后端
        // 一直误报（如 QNetworkInformation 插件缺失）导致永远拉不到天气。
        ++m_offlineSkips;
        if (m_offlineSkips >= 3) {
            m_offlineSkips = 0;
            m_inFlight = true;
            m_provider->fetchCurrent();
            return;
        }
        // 其余情况按短间隔重试，网络一恢复下一次尝试就拉取。
        if (!m_retryTimer.isActive())
            m_retryTimer.start();
        return;
    }
    m_offlineSkips = 0;
    m_inFlight = true;
    m_provider->fetchCurrent();
}

void WeatherService::handleCurrentWeather(const WeatherSnapshot& snapshot)
{
    m_inFlight = false;
    if (!m_started)
        return;

    // 拿到第一条数据后停止短间隔重试，交回 refresh_minutes 正常节奏。
    m_retryTimer.stop();

    WeatherSnapshot stored = snapshot;
    stored.valid = true;
    // updatedAt 优先用快照自带值（QWeatherProvider 会填 UTC 当前时间）；
    // 无值时才本地标记，统一使用 UTC。
    stored.updatedAt = snapshot.updatedAt.isValid()
        ? snapshot.updatedAt : QDateTime::currentDateTimeUtc();
    stored.stale = false;
    stored.latitude = m_configuration.latitude;
    stored.longitude = m_configuration.longitude;

    m_last = stored;
    emit weatherUpdated(stored);

    if (m_systemService)
        m_systemService->setWeatherSummary(stored.summary(), stored.conditionCode);
    qInfo().noquote()
        << "Weather updated:" << stored.summary()
        << "observedAt=" << stored.observedAt.toString(Qt::ISODate);
}

void WeatherService::handleFetchFailed(const WeatherError& error)
{
    m_inFlight = false;
    // 首次拉取失败（例如网络宣称可用但 WiFi 实际还没通）也按短间隔重试，
    // 直到拿到第一条数据；已有数据时按 refresh_minutes 节奏即可。
    if (!m_last && !m_retryTimer.isActive())
        m_retryTimer.start();
    // 失败时保留旧数据；若从未成功过，SystemService 仍保持默认 "--"。
    // 日志只输出 provider / 错误码 / HTTP 状态 / 响应 code，不得输出 API Key。
    qWarning().noquote()
        << "Weather update failed: provider=" << error.provider
        << "code=" << weatherErrorCodeName(error.code)
        << "http=" << error.httpStatus
        << "api_code=" << error.apiCode
        << "detail=" << error.diagnostic;
}

void WeatherService::handleSystemStatus(const SystemStatus& status)
{
    // 网络由不可用变为可用（上升沿）时立即补刷：板子开机时 wlan0 通常比
    // 应用晚就绪，启动即刷会被离线跳过，没有这条补刷，下一次尝试要等
    // refresh_minutes（默认 30 分钟），表现为"永远没有天气"。
    const bool nowAvailable = status.networkAvailable;
    const bool risingEdge = nowAvailable && !m_networkWasAvailable;
    m_networkWasAvailable = nowAvailable;
    if (risingEdge && m_started)
        refresh();
}

void WeatherService::scheduleNextRefresh()
{
    m_refreshTimer.start(m_configuration.refreshMinutes * 60 * 1'000);
}

bool WeatherService::isStale(const QDateTime& updatedAt) const
{
    if (!updatedAt.isValid())
        return true;
    return updatedAt.secsTo(QDateTime::currentDateTime()) > m_configuration.staleAfterMinutes * 60;
}
