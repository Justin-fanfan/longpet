#include "data/WeatherConfigRepository.h"

#include <QByteArray>
#include <QDebug>
#include <QFileInfo>
#include <QSettings>

#include <utility>

namespace {
QString environmentOrValue(const char* environmentName, const QString& value)
{
    const QString environment = qEnvironmentVariable(environmentName).trimmed();
    return environment.isEmpty() ? value.trimmed() : environment;
}

bool environmentBool(const char* environmentName, bool fallback)
{
    const QString value = qEnvironmentVariable(environmentName).trimmed().toLower();
    if (value.isEmpty())
        return fallback;
    if (value == QStringLiteral("1") || value == QStringLiteral("true")
        || value == QStringLiteral("yes") || value == QStringLiteral("on"))
        return true;
    if (value == QStringLiteral("0") || value == QStringLiteral("false")
        || value == QStringLiteral("no") || value == QStringLiteral("off"))
        return false;
    return fallback;
}

int environmentInteger(const char* environmentName, int fallback)
{
    bool valid = false;
    const int value = qEnvironmentVariableIntValue(environmentName, &valid);
    return valid ? value : fallback;
}

double environmentDouble(const char* environmentName, double fallback)
{
    bool valid = false;
    const double value = qEnvironmentVariable(environmentName)
        .trimmed().toDouble(&valid);
    return valid ? value : fallback;
}

double boundedDouble(QSettings& settings, const QString& key, double fallback)
{
    bool valid = false;
    const double value = settings.value(key, fallback).toString().toDouble(&valid);
    return valid ? value : fallback;
}

int boundedInteger(QSettings& settings, const QString& key, int fallback)
{
    bool valid = false;
    const int value = settings.value(key, fallback).toInt(&valid);
    return valid ? value : fallback;
}
}

WeatherConfigRepository::WeatherConfigRepository(QString path)
    : m_path(std::move(path))
{
}

WeatherConfiguration WeatherConfigRepository::load(QString* error) const
{
    if (error)
        error->clear();

    WeatherConfiguration configuration;
    const QString prefix = QStringLiteral("weather/");

    QSettings settings(m_path, QSettings::IniFormat);
    const bool hasGroup = settings.childGroups().contains(
        QStringLiteral("weather"), Qt::CaseInsensitive);

    configuration.enabled = environmentBool(
        "LONGPET_WEATHER_ENABLED", hasGroup
            ? settings.value(prefix + QStringLiteral("enabled"), false).toBool()
            : false);
    configuration.provider = environmentOrValue(
        "LONGPET_WEATHER_PROVIDER", hasGroup
            ? settings.value(prefix + QStringLiteral("provider"),
                             QStringLiteral("qweather")).toString()
            : QStringLiteral("qweather"));
    configuration.apiHost = environmentOrValue(
        "LONGPET_WEATHER_API_HOST", hasGroup
            ? settings.value(prefix + QStringLiteral("api_host")).toString()
            : QString());
    configuration.apiKey = environmentOrValue(
        "LONGPET_WEATHER_API_KEY", hasGroup
            ? settings.value(prefix + QStringLiteral("api_key")).toString()
            : QString());
    configuration.latitude = environmentDouble(
        "LONGPET_WEATHER_LATITUDE", hasGroup
            ? boundedDouble(settings, prefix + QStringLiteral("latitude"), 0.0)
            : 0.0);
    configuration.longitude = environmentDouble(
        "LONGPET_WEATHER_LONGITUDE", hasGroup
            ? boundedDouble(settings, prefix + QStringLiteral("longitude"), 0.0)
            : 0.0);
    configuration.language = environmentOrValue(
        "LONGPET_WEATHER_LANGUAGE", hasGroup
            ? settings.value(prefix + QStringLiteral("language"),
                             QStringLiteral("zh")).toString()
            : QStringLiteral("zh"));
    configuration.refreshMinutes = environmentInteger(
        "LONGPET_WEATHER_REFRESH_MINUTES", hasGroup
            ? boundedInteger(settings, prefix + QStringLiteral("refresh_minutes"), 30)
            : 30);
    configuration.requestTimeoutMs = environmentInteger(
        "LONGPET_WEATHER_REQUEST_TIMEOUT_MS", hasGroup
            ? boundedInteger(settings, prefix + QStringLiteral("request_timeout_ms"), 10'000)
            : 10'000);
    configuration.staleAfterMinutes = environmentInteger(
        "LONGPET_WEATHER_STALE_AFTER_MINUTES", hasGroup
            ? boundedInteger(settings, prefix + QStringLiteral("stale_after_minutes"), 120)
            : 120);
    configuration.refreshMinutes = qMax(1, configuration.refreshMinutes);
    configuration.requestTimeoutMs = qMax(1'000, configuration.requestTimeoutMs);
    configuration.staleAfterMinutes = qMax(1, configuration.staleAfterMinutes);

    // 未启用时不报告配置缺失，只提示“天气未启用”。
    if (!configuration.enabled) {
        if (error)
            *error = QStringLiteral("天气功能未启用");
        return configuration;
    }

    const QString validationError = configuration.validationError();
    if (error && !validationError.isEmpty()) {
        const QString prefixText = QFileInfo::exists(m_path)
            ? QStringLiteral("天气配置无效")
            : QStringLiteral("未找到天气配置文件 %1").arg(m_path);
        *error = QStringLiteral("%1：%2").arg(prefixText, validationError);
    }
    return configuration;
}

QString WeatherConfigRepository::path() const
{
    return m_path;
}
