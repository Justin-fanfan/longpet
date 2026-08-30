#pragma once

#include "model/SystemModels.h"
#include "model/WeatherModels.h"

#include <QObject>
#include <QTimer>

#include <optional>

class SystemService;
class WeatherProviderPort;

// 天气业务服务：负责定时刷新、启动立即刷新、缓存最近一次成功快照、数据过期
// 判断、离线时跳过请求、失败时保留旧数据。只依赖 WeatherProviderPort 抽象，
// 不直接依赖具体 Provider。更新成功后调用 SystemService::setWeatherSummary，
// 状态栏展示链路保持不变。
//
// 启动竞态：板子开机时 wlan0/NetworkManager 尚未就绪，启动即刷新会被离线跳过，
// 若没有额外机制，下次尝试要等 refresh_minutes（表现=没有天气）。因此：
//   - 监听 SystemService::statusChanged，网络从不可用变为可用（上升沿）时立即补刷；
//   - 尚未取得任何数据时，离线跳过 / 拉取失败都以短间隔（m_retryTimer）重试，
//     直到第一次成功。
class WeatherService final : public QObject {
    Q_OBJECT

public:
    WeatherService(WeatherConfiguration configuration,
                   WeatherProviderPort* provider,
                   SystemService* systemService,
                   QObject* parent = nullptr);
    ~WeatherService() override;

    void start();
    void stop();
    bool isActive() const;

    WeatherSnapshot current() const;
    std::optional<WeatherSnapshot> currentOrNone() const;
    QString currentSummary() const;

signals:
    void weatherUpdated(const WeatherSnapshot& snapshot);

private:
    void refresh();
    void handleCurrentWeather(const WeatherSnapshot& snapshot);
    void handleFetchFailed(const WeatherError& error);
    void handleSystemStatus(const SystemStatus& status);
    void scheduleNextRefresh();
    bool isStale(const QDateTime& updatedAt) const;

    WeatherConfiguration m_configuration;
    WeatherProviderPort* m_provider = nullptr;
    SystemService* m_systemService = nullptr;
    std::optional<WeatherSnapshot> m_last;
    QTimer m_refreshTimer;
    QTimer m_retryTimer;
    bool m_started = false;
    bool m_connected = false;
    bool m_inFlight = false;
    bool m_networkWasAvailable = false;
    // 连续离线判定次数：达到阈值（3）且无数据时强行探测一次。
    int m_offlineSkips = 0;
};
