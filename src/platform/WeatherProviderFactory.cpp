#include "platform/WeatherProviderFactory.h"

#include "platform/QWeatherProvider.h"
#include "services/WeatherPorts.h"

#include <QDebug>

#include <utility>

namespace {
QString normalizedProvider(const QString& provider)
{
    return provider.trimmed().toLower();
}

// 当 provider 不受支持或未配置时，返回一个“请求即失败”的占位 Provider，
// 避免主程序启动受影响；错误在请求时通过 fetchFailed 上报。
class UnsupportedWeatherProvider final : public WeatherProviderPort {
public:
    explicit UnsupportedWeatherProvider(QString provider)
        : m_provider(std::move(provider)) {}

    void fetchCurrent() override
    {
        WeatherError error;
        error.code = WeatherErrorCode::ConfigurationError;
        error.provider = m_provider.isEmpty()
            ? QStringLiteral("unconfigured") : m_provider;
        error.diagnostic = QStringLiteral("unsupported weather provider=%1")
            .arg(m_provider);
        emit fetchFailed(error);
    }
    void cancel() override {}

private:
    QString m_provider;
};
}

std::unique_ptr<WeatherProviderPort> WeatherProviderFactory::create(
    const WeatherConfiguration& configuration)
{
    const QString provider = normalizedProvider(configuration.provider);
    qInfo().noquote() << "Weather provider:" << provider;
    if (provider == QStringLiteral("qweather"))
        return std::make_unique<QWeatherProvider>(configuration);
    return std::make_unique<UnsupportedWeatherProvider>(provider);
}
