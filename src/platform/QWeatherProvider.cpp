#include "platform/QWeatherProvider.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <cmath>
#include <utility>

namespace {
QString coordinate(double value)
{
    // 避免输出 31.230000 这类冗余尾零，同时保留足够精度给和风。
    return QString::number(value, 'g', 8);
}

// 和风新版 API 的 humidity 为 [0,1] 小数（如 0.69 = 69%）。
// 统一换算为 0-100 的百分比整数；容忍个别厂商直接返回百分比（0-100）。
int normalizeHumidityPercent(double raw)
{
    if (raw <= 0.0)
        return 0;
    if (raw <= 1.0)
        return qRound(raw * 100.0);
    if (raw <= 100.0)
        return qRound(raw);
    return 100;
}

// 新版 /weather/v1/current 的数值字段（temp/feelsLike/humidity）是 JSON 数字
// （如 "temp":26、"humidity":0.69），旧接口或代理可能返回字符串。
// 两种形态都必须容忍：统一转成文本 / 数字，避免上板时因 .toString() 取空
// 而误判 "missing now.temp"（这正是状态栏无天气数据的常见原因）。
QString scalarText(const QJsonValue& value)
{
    if (value.isString())
        return value.toString();
    if (value.isDouble())
        return QString::number(value.toDouble());
    if (value.isBool())
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    return {};
}

double scalarDouble(const QJsonValue& value)
{
    if (value.isDouble())
        return value.toDouble();
    return value.toString().toDouble();
}

// 全字段构造 WeatherError，避免部分初始化列表触发
// -Wmissing-field-initializers（apiCode 无默认成员初始化器）。
WeatherError makeError(WeatherErrorCode code, const QString& provider,
                       const QString& diagnostic, const QString& apiCode = {},
                       int httpStatus = 0)
{
    WeatherError error;
    error.code = code;
    error.provider = provider;
    error.diagnostic = diagnostic;
    error.apiCode = apiCode;
    error.httpStatus = httpStatus;
    return error;
}
}

QWeatherProvider::QWeatherProvider(WeatherConfiguration configuration,
                                   QObject* parent)
    : WeatherProviderPort(parent),
      m_configuration(std::move(configuration))
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(qMax(1'000, m_configuration.requestTimeoutMs));
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (!m_reply)
            return;
        m_timedOut = true;
        m_reply->abort();
    });
}

QWeatherProvider::~QWeatherProvider()
{
    cancel();
}

QUrl QWeatherProvider::currentUrl() const
{
    // 控制台「设置」给出的专属 API Host 通常不带协议头（如 ph6vhhujph.re.qweatherapi.com），
    // 直接当 URL 会得到空 scheme，QNetworkAccessManager 报 "Protocol \"\" is unknown"，
    // 请求根本出不去。未带协议头时统一补 https://。
    QString host = m_configuration.apiHost.trimmed();
    if (!host.isEmpty() && !host.contains(QStringLiteral("://")))
        host.prepend(QStringLiteral("https://"));
    QUrl url(host);
    QString path = url.path();
    if (!path.endsWith(QLatin1Char('/')))
        path.append(QLatin1Char('/'));
    path += QStringLiteral("weather/v1/current/%1/%2")
        .arg(coordinate(m_configuration.latitude),
             coordinate(m_configuration.longitude));
    url.setPath(path);
    if (!m_configuration.language.trimmed().isEmpty())
        url.setQuery(QStringLiteral("lang=%1").arg(m_configuration.language));
    return url;
}

QNetworkRequest QWeatherProvider::requestFor(const QUrl& url) const
{
    QNetworkRequest request(url);
    // Key 放请求头，不放进 URL query，避免被日志或抓包泄漏。
    request.setRawHeader(QByteArrayLiteral("X-QW-Api-Key"),
                         m_configuration.apiKey.toUtf8());
    request.setRawHeader(QByteArrayLiteral("Accept"),
                         QByteArrayLiteral("application/json"));
    return request;
}

void QWeatherProvider::fetchCurrent()
{
    if (m_reply) {
        // 上一次还没结束，先取消，避免并发请求。
        m_reply->abort();
        m_reply->deleteLater();
        m_reply.clear();
        m_timedOut = false;
    }
    m_timeout.start();
    QNetworkReply* reply = m_network.get(requestFor(currentUrl()));
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        handleFinished(reply);
    });
}

void QWeatherProvider::cancel()
{
    m_timeout.stop();
    if (m_reply) {
        const bool wasTimedOut = m_timedOut;
        disconnect(m_reply, nullptr, this, nullptr);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply.clear();
        m_timedOut = false;
        if (!wasTimedOut)
            emit fetchFailed(makeError(WeatherErrorCode::Cancelled,
                                       m_configuration.provider,
                                       QStringLiteral("request cancelled")));
    }
}

void QWeatherProvider::handleFinished(QNetworkReply* reply)
{
    if (reply != m_reply)
        return;
    m_timeout.stop();
    const bool timedOut = m_timedOut;
    m_reply.clear();
    m_timedOut = false;
    const int httpStatus = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->isOpen() ? reply->readAll() : QByteArray();
    const QString networkErrorText = reply->errorString();
    const QNetworkReply::NetworkError networkError = reply->error();
    reply->deleteLater();

    if (timedOut) {
        fail(WeatherErrorCode::Timeout,
             QStringLiteral("request timed out after %1 ms")
                 .arg(qMax(1'000, m_configuration.requestTimeoutMs)),
             {}, httpStatus);
        return;
    }
    if (httpStatus > 0 && (httpStatus < 200 || httpStatus >= 300)) {
        // 新 Host 的错误响应可能是 application/problem+json，
        // 提取 title/detail 作为安全诊断信息（不含 API Key）。
        QString diagnostic = problemDetail(body);
        if (diagnostic.isEmpty())
            diagnostic = QStringLiteral("HTTP %1").arg(httpStatus);
        else
            diagnostic.prepend(QStringLiteral("HTTP %1: ").arg(httpStatus));
        fail(mapHttpStatus(httpStatus), diagnostic, {}, httpStatus);
        return;
    }
    if (networkError != QNetworkReply::NoError) {
        QString diagnostic = networkErrorText;
        const QString detail = problemDetail(body);
        if (!detail.isEmpty())
            diagnostic += QStringLiteral(" (%1)").arg(detail);
        fail(WeatherErrorCode::NetworkError, diagnostic, {}, httpStatus);
        return;
    }

    WeatherSnapshot snapshot;
    WeatherError parseError;
    if (parseCurrent(body, m_configuration, &snapshot, &parseError)) {
        emit currentWeatherReady(snapshot);
        return;
    }
    fail(parseError.code, parseError.diagnostic, parseError.apiCode, httpStatus);
}

void QWeatherProvider::fail(WeatherErrorCode code, const QString& diagnostic,
                            const QString& apiCode, int httpStatus)
{
    WeatherError error;
    error.code = code;
    error.provider = m_configuration.provider;
    error.diagnostic = diagnostic.left(500);
    error.apiCode = apiCode;
    error.httpStatus = httpStatus;
    emit fetchFailed(error);
}

WeatherErrorCode QWeatherProvider::mapResponseCode(const QString& code)
{
    // 和风响应 code：200 成功，401 鉴权失败，402 配额不足，403 权限禁止，
    // 404 资源/参数错误（多为错误专属 Host），429 频率限制，5xx 服务端错误。
    if (code == QStringLiteral("200"))
        return WeatherErrorCode::ServerError;   // 调用方不应走到这里
    if (code == QStringLiteral("401"))
        return WeatherErrorCode::Unauthorized;
    if (code == QStringLiteral("402") || code == QStringLiteral("429"))
        return WeatherErrorCode::RateLimited;
    if (code == QStringLiteral("403") || code == QStringLiteral("404"))
        return WeatherErrorCode::ConfigurationError;
    if (code.startsWith(QLatin1Char('5')))
        return WeatherErrorCode::ServerError;
    return WeatherErrorCode::InvalidResponse;
}

WeatherErrorCode QWeatherProvider::mapHttpStatus(int httpStatus)
{
    // 401 仅当鉴权失败；403/404 归配置错误（专属 Host 分配、Key 权限等）。
    if (httpStatus == 401)
        return WeatherErrorCode::Unauthorized;
    if (httpStatus == 429)
        return WeatherErrorCode::RateLimited;
    if (httpStatus >= 500)
        return WeatherErrorCode::ServerError;
    if (httpStatus >= 400)
        return WeatherErrorCode::ConfigurationError;
    return WeatherErrorCode::ServerError;
}

QString QWeatherProvider::problemDetail(const QByteArray& body)
{
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (!document.isObject())
        return {};

    const QJsonObject root = document.object();
    // application/problem+json：{"error":{"status":...,"type":...,"title":...,"detail":...}}
    // type 通常是超链接，日志只保留可读的 title/detail。
    const QJsonObject problem = root.value(QStringLiteral("error")).toObject();
    if (!problem.isEmpty()) {
        const QString title = problem.value(QStringLiteral("title")).toString();
        const QString detail = problem.value(QStringLiteral("detail")).toString();
        if (!title.isEmpty() && !detail.isEmpty())
            return title + QStringLiteral(": ") + detail;
        if (!title.isEmpty())
            return title;
        return detail;
    }

    // 兼容 {"code":"...","message":"..."} 形式。
    return root.value(QStringLiteral("message")).toString();
}

bool QWeatherProvider::parseCurrent(const QByteArray& jsonBody,
                                    const WeatherConfiguration& configuration,
                                    WeatherSnapshot* snapshot,
                                    WeatherError* error)
{
    if (error)
        *error = {};

    if (jsonBody.trimmed().isEmpty()) {
        if (error)
            *error = makeError(WeatherErrorCode::EmptyResult,
                               configuration.provider,
                               QStringLiteral("empty response body"));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(jsonBody, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = makeError(WeatherErrorCode::InvalidResponse,
                               configuration.provider,
                               QStringLiteral("JSON error: %1").arg(parseError.errorString()));
        return false;
    }

    const QJsonObject root = document.object();

    // 版面实测（2026-08）：新版专属 Host 响应为顶层对象，且没有顶层 code 字段：
    //   {"metadata":{...},
    //    "condition":{"text":"阴","code":"104"},
    //    "temperature":{"value":29.02,"unit":"°C"},
    //    "feelsLike":{"value":32.05,"unit":"°C"},
    //    "humidity":0.8, "wind": {...}, "pressure": {...}, ...}
    // 旧接口（devapi v7，now 形态）才有 code/now。有 code 时必须为 "200"；
    // 没有 code 按新版成功处理（错误通过 HTTP status / problem+json 表达）。

    const QJsonValue codeValue = root.value(QStringLiteral("code"));
    if (!codeValue.isUndefined() && !codeValue.isNull()) {
        const QString code = scalarText(codeValue);
        if (code.isEmpty() || code != QStringLiteral("200")) {
            if (error)
                *error = makeError(mapResponseCode(code), configuration.provider,
                                   QStringLiteral("QWeather response code=%1").arg(code),
                                   code);
            return false;
        }
    }

    WeatherSnapshot result;
    result.valid = true;

    // 新版形态：temperature.value 必填，condition/humidity 可选。
    const QJsonValue temperatureValue =
        root.value(QStringLiteral("temperature")).toObject()
            .value(QStringLiteral("value"));
    if (!temperatureValue.isUndefined() && !temperatureValue.isNull()) {
        result.temperatureC = scalarDouble(temperatureValue);
        result.feelsLikeC = scalarDouble(
            root.value(QStringLiteral("feelsLike")).toObject()
                .value(QStringLiteral("value")));
        const QJsonObject condition =
            root.value(QStringLiteral("condition")).toObject();
        result.condition = scalarText(condition.value(QStringLiteral("text")));
        result.conditionCode = scalarText(condition.value(QStringLiteral("code")));
        result.humidityPercent = normalizeHumidityPercent(
            scalarDouble(root.value(QStringLiteral("humidity"))));
    } else {
        // 兼容旧形态（devapi v7 / now 对象），humidity 归一化相同。
        const QJsonObject now = root.value(QStringLiteral("now")).toObject();
        if (now.isEmpty()) {
            if (error)
                *error = makeError(WeatherErrorCode::EmptyResult,
                                   configuration.provider,
                                   QStringLiteral("missing temperature or now object"));
            return false;
        }
        const QJsonValue tempValue = now.value(QStringLiteral("temp"));
        if (!tempValue.isDouble() && scalarText(tempValue).isEmpty()) {
            if (error)
                *error = makeError(WeatherErrorCode::InvalidResponse,
                                   configuration.provider,
                                   QStringLiteral("missing now.temp"));
            return false;
        }
        result.temperatureC = scalarDouble(tempValue);
        result.feelsLikeC = scalarDouble(now.value(QStringLiteral("feelsLike")));
        result.condition = scalarText(now.value(QStringLiteral("text")));
        result.conditionCode = scalarText(now.value(QStringLiteral("icon")));
        result.humidityPercent = normalizeHumidityPercent(
            scalarDouble(now.value(QStringLiteral("humidity"))));
    }

    result.latitude = configuration.latitude;
    result.longitude = configuration.longitude;

    // 新版 /weather/v1/current 响应没有 v7 的 obsTime/updateTime 字段：
    // observedAt 保持 invalid；updatedAt 用本机本次成功获取响应的 UTC 时间。
    // stale 判断只依赖最后一次成功获取时间（WeatherService::isStale），不依赖 API 字段。
    result.updatedAt = QDateTime::currentDateTimeUtc();

    if (snapshot)
        *snapshot = result;
    return true;
}
