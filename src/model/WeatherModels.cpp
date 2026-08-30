#include "model/WeatherModels.h"

#include <cmath>

namespace {
bool inRange(double value, double minimum, double maximum)
{
    return std::isfinite(value) && value >= minimum && value <= maximum;
}
}

QString WeatherSnapshot::summary() const
{
    if (!valid)
        return {};
    const QString temperature = QString::number(qRound(temperatureC));
    if (condition.trimmed().isEmpty())
        return QStringLiteral("%1°").arg(temperature);
    return QStringLiteral("%1 %2°").arg(condition.trimmed(), temperature);
}

bool WeatherConfiguration::hasValidLocation() const
{
    // (0, 0) 是合法坐标，不因经纬度恰好为零而判非法；只做范围校验。
    return inRange(latitude, -90.0, 90.0) && inRange(longitude, -180.0, 180.0);
}

QString WeatherConfiguration::validationError() const
{
    if (!enabled)
        return {};
    if (provider.trimmed().isEmpty())
        return QStringLiteral("天气 Provider 未配置");
    if (apiHost.trimmed().isEmpty())
        return QStringLiteral("天气 API Host 未配置");
    if (apiKey.trimmed().isEmpty())
        return QStringLiteral("天气 API Key 未配置");
    if (!hasValidLocation())
        return QStringLiteral("天气经纬度未配置或无效");
    return {};
}

QString weatherErrorCodeName(WeatherErrorCode code)
{
    switch (code) {
    case WeatherErrorCode::Disabled:
        return QStringLiteral("Disabled");
    case WeatherErrorCode::ConfigurationError:
        return QStringLiteral("ConfigurationError");
    case WeatherErrorCode::NetworkUnavailable:
        return QStringLiteral("NetworkUnavailable");
    case WeatherErrorCode::NetworkError:
        return QStringLiteral("NetworkError");
    case WeatherErrorCode::Timeout:
        return QStringLiteral("Timeout");
    case WeatherErrorCode::Unauthorized:
        return QStringLiteral("Unauthorized");
    case WeatherErrorCode::RateLimited:
        return QStringLiteral("RateLimited");
    case WeatherErrorCode::ServerError:
        return QStringLiteral("ServerError");
    case WeatherErrorCode::InvalidResponse:
        return QStringLiteral("InvalidResponse");
    case WeatherErrorCode::EmptyResult:
        return QStringLiteral("EmptyResult");
    case WeatherErrorCode::Cancelled:
        return QStringLiteral("Cancelled");
    }
    return QStringLiteral("Unknown");
}
