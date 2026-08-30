#pragma once

#include "model/WeatherModels.h"
#include "services/WeatherPorts.h"

#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>

class QNetworkReply;
class QNetworkRequest;
class QUrl;

class QWeatherProvider final : public WeatherProviderPort {
    Q_OBJECT

public:
    explicit QWeatherProvider(WeatherConfiguration configuration,
                              QObject* parent = nullptr);
    ~QWeatherProvider() override;

    void fetchCurrent() override;
    void cancel() override;

    // 可测试的解析辅助：把和风 /weather/v1/current 的 JSON body 解析成快照。
    // 成功时返回 true 并填充 snapshot；失败时返回 false 并通过 code/apiCode 说明原因。
    static bool parseCurrent(const QByteArray& jsonBody,
                             const WeatherConfiguration& configuration,
                             WeatherSnapshot* snapshot,
                             WeatherError* error);

    // 把和风响应 code 字符串（"200"/"401"/"429"...）映射为错误码。
    static WeatherErrorCode mapResponseCode(const QString& code);

    // 把 HTTP 状态码映射为错误码。
    // 400/403/404（不含 401/429）归 ConfigurationError——多为配置项错误（Host/Key/参数），
    // 404 明确不再映射为 Unauthorized。
    static WeatherErrorCode mapHttpStatus(int httpStatus);

    // 尝试从 application/problem+json（或 {"code":...,"message":...}）错误响应中
    // 提取安全的诊断文本（title/detail），失败返回空串。不会输出 API Key。
    static QString problemDetail(const QByteArray& body);

    QUrl currentUrl() const;

private:
    QNetworkRequest requestFor(const QUrl& url) const;
    void handleFinished(QNetworkReply* reply);
    void fail(WeatherErrorCode code, const QString& diagnostic,
              const QString& apiCode = {}, int httpStatus = 0);

    WeatherConfiguration m_configuration;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QTimer m_timeout;
    bool m_timedOut = false;
};
