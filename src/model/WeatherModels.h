#pragma once

#include <QDateTime>
#include <QString>

// 天气错误码。独立于 AiProviderErrorCode，避免把天气模块和 AI 服务耦合。
enum class WeatherErrorCode {
    Disabled,
    ConfigurationError,
    NetworkUnavailable,
    NetworkError,
    Timeout,
    Unauthorized,
    RateLimited,
    ServerError,
    InvalidResponse,
    EmptyResult,
    Cancelled
};

struct WeatherError {
    WeatherErrorCode code = WeatherErrorCode::ServerError;
    QString provider;
    QString diagnostic;
    QString apiCode;
    int httpStatus = 0;
};

// 结构化天气快照。既有给状态栏用的 summary()，也保留完整字段供以后
// 扩展每日/小时预报、动态图标、空气质量和语音上下文使用。
struct WeatherSnapshot {
    bool valid = false;

    QString condition;
    QString conditionCode;   // 和风 icon 编码，为以后晴/阴/雨/雪动态图标预留

    double temperatureC = 0.0;
    double feelsLikeC = 0.0;
    // 湿度百分比（0-100）。和风新版 API 返回 [0,1] 的小数（如 0.69），
    // 解析时统一换算成 69 保存，语义与显示层（"湿度：69%"）保持一致。
    int humidityPercent = 0;

    double latitude = 0.0;
    double longitude = 0.0;

    QDateTime observedAt;    // 服务端观测时间；新版接口未提供时保持 invalid
    QDateTime updatedAt;     // 本机最近一次成功获取响应的 UTC 时间
    bool stale = false;      // 超过 stale_after_minutes 后置为 true

    QString summary() const;
};

// 天气模块配置。配置来自独立 /etc/longpet/longpet-weather.ini，环境变量优先。
struct WeatherConfiguration {
    bool enabled = false;
    QString provider;
    QString apiHost;         // 和风用户控制台分配的专属 API Host，如 https://abc.def.qweatherapi.com
    QString apiKey;
    double latitude = 0.0;
    double longitude = 0.0;
    QString language = QStringLiteral("zh");
    int refreshMinutes = 30;
    int requestTimeoutMs = 10'000;
    int staleAfterMinutes = 120;

    QString validationError() const;
    bool isValid() const { return validationError().isEmpty(); }
    bool hasValidLocation() const;
};

QString weatherErrorCodeName(WeatherErrorCode code);

Q_DECLARE_METATYPE(WeatherErrorCode)
Q_DECLARE_METATYPE(WeatherError)
Q_DECLARE_METATYPE(WeatherSnapshot)
