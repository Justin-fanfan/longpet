#include "ProviderHttpClient.h"

#include <QCoreApplication>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <utility>

namespace {
struct ApiErrorDetails {
    QString code;
    QString message;
};

ApiErrorDetails apiErrorDetails(const QByteArray& body)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return {{}, QString::fromUtf8(body.left(300)).trimmed()};

    const QJsonObject root = document.object();
    ApiErrorDetails details {
        root.value(QStringLiteral("code")).toString().trimmed(),
        root.value(QStringLiteral("message")).toString().trimmed()
    };
    const QJsonValue errorValue = root.value(QStringLiteral("error"));
    if (errorValue.isString() && details.message.isEmpty())
        details.message = errorValue.toString().trimmed();
    if (errorValue.isObject()) {
        const QJsonObject error = errorValue.toObject();
        if (details.code.isEmpty()) {
            details.code = error.value(QStringLiteral("code")).toString().trimmed();
            if (details.code.isEmpty())
                details.code = error.value(QStringLiteral("type")).toString().trimmed();
        }
        if (details.message.isEmpty())
            details.message = error.value(QStringLiteral("message")).toString().trimmed();
    }
    return details;
}

QString userMessageFor(AiProviderErrorCode code)
{
    switch (code) {
    case AiProviderErrorCode::NetworkError:
        return QStringLiteral("网络连接失败，请检查 Wi-Fi");
    case AiProviderErrorCode::Timeout:
        return QStringLiteral("请求超时，请稍后再试");
    case AiProviderErrorCode::Unauthorized:
        return QStringLiteral("AI 服务鉴权失败，请检查配置");
    case AiProviderErrorCode::RateLimited:
        return QStringLiteral("AI 服务繁忙，请稍后再试");
    case AiProviderErrorCode::ServerError:
        return QStringLiteral("AI 服务暂时不可用，请稍后再试");
    case AiProviderErrorCode::InvalidResponse:
        return QStringLiteral("AI 服务返回内容异常，请稍后再试");
    case AiProviderErrorCode::EmptyResult:
        return QStringLiteral("AI 服务没有返回有效结果");
    case AiProviderErrorCode::UnsupportedProvider:
        return QStringLiteral("当前 AI Provider 不受支持");
    case AiProviderErrorCode::Cancelled:
        return QStringLiteral("本次对话已取消");
    }
    return QStringLiteral("AI 服务暂时不可用，请稍后再试");
}
}

ProviderHttpClient::ProviderHttpClient(QString providerName, int timeoutMs,
                                       QObject* parent)
    : QObject(parent),
      m_providerName(std::move(providerName)),
      m_timeoutMs(qMax(1'000, timeoutMs))
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(m_timeoutMs);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (!m_reply)
            return;
        m_timedOut = true;
        m_reply->abort();
    });
}

void ProviderHttpClient::postJson(quint64 sessionId, const QUrl& url,
                                  const QString& apiKey, const QByteArray& json,
                                  const Headers& extraHeaders)
{
    if (m_reply)
        cancel(m_sessionId);
    QNetworkRequest request = requestFor(
        url, apiKey, QByteArrayLiteral("application/json"), extraHeaders);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    watch(m_network.post(request, json), sessionId);
}

void ProviderHttpClient::postMultipart(quint64 sessionId, const QUrl& url,
                                       const QString& apiKey,
                                       QHttpMultiPart* multipart,
                                       const Headers& extraHeaders)
{
    if (m_reply)
        cancel(m_sessionId);
    QNetworkReply* reply = m_network.post(requestFor(
        url, apiKey, QByteArrayLiteral("application/json"), extraHeaders), multipart);
    multipart->setParent(reply);
    watch(reply, sessionId);
}

void ProviderHttpClient::getBinary(quint64 sessionId, const QUrl& url,
                                   const Headers& extraHeaders)
{
    if (m_reply)
        cancel(m_sessionId);
    watch(m_network.get(requestFor(
        url, {}, QByteArrayLiteral("audio/*,application/octet-stream"), extraHeaders)),
        sessionId);
}

void ProviderHttpClient::cancel(quint64 sessionId)
{
    if (!m_reply || sessionId != m_sessionId)
        return;
    m_timeout.stop();
    disconnect(m_reply, nullptr, this, nullptr);
    m_reply->abort();
    m_reply->deleteLater();
    m_reply.clear();
    m_timedOut = false;
}

QNetworkRequest ProviderHttpClient::requestFor(
    const QUrl& url, const QString& apiKey, const QByteArray& accept,
    const Headers& extraHeaders) const
{
    QNetworkRequest request(url);
    request.setRawHeader(QByteArrayLiteral("Accept"), accept);
    const QByteArray version = QCoreApplication::applicationVersion().toUtf8();
    request.setRawHeader(QByteArrayLiteral("User-Agent"),
                         QByteArrayLiteral("LongPet/")
                             + (version.isEmpty() ? QByteArrayLiteral("unknown") : version));
    if (!apiKey.isEmpty()) {
        request.setRawHeader(QByteArrayLiteral("Authorization"),
                             QByteArrayLiteral("Bearer ") + apiKey.toUtf8());
    }
    for (const auto& header : extraHeaders)
        request.setRawHeader(header.first, header.second);
    return request;
}

void ProviderHttpClient::watch(QNetworkReply* reply, quint64 sessionId)
{
    m_reply = reply;
    m_sessionId = sessionId;
    m_timedOut = false;
    m_timeout.start();
    connect(reply, &QNetworkReply::finished,
            this, [this, reply] { handleFinished(reply); });
}

void ProviderHttpClient::handleFinished(QNetworkReply* reply)
{
    if (reply != m_reply)
        return;
    m_timeout.stop();
    const quint64 sessionId = m_sessionId;
    const bool timedOut = m_timedOut;
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkErrorText = reply->errorString();
    ProviderHttpResponse response;
    response.statusCode = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
    response.contentType = reply->rawHeader(
        QByteArrayLiteral("Content-Type")).toLower();
    response.body = reply->isOpen() ? reply->readAll() : QByteArray();
    m_reply.clear();
    m_timedOut = false;
    reply->deleteLater();

    if (timedOut) {
        emit failed(sessionId, providerResponseError(
            AiProviderErrorCode::Timeout, m_providerName,
            userMessageFor(AiProviderErrorCode::Timeout),
            QStringLiteral("request timed out after %1 ms").arg(m_timeoutMs)));
        return;
    }
    if (response.statusCode > 0
        && (response.statusCode < 200 || response.statusCode >= 300)) {
        emit failed(sessionId, httpFailure(response.statusCode, response.body));
        return;
    }
    if (networkError != QNetworkReply::NoError) {
        const AiProviderErrorCode code = networkError == QNetworkReply::OperationCanceledError
            ? AiProviderErrorCode::Cancelled : AiProviderErrorCode::NetworkError;
        emit failed(sessionId, providerResponseError(
            code, m_providerName, userMessageFor(code),
            QStringLiteral("network_error=%1").arg(networkErrorText)));
        return;
    }
    emit succeeded(sessionId, response);
}

AiProviderError ProviderHttpClient::httpFailure(int status,
                                                const QByteArray& body) const
{
    AiProviderErrorCode code = AiProviderErrorCode::InvalidResponse;
    if (status == 401 || status == 403)
        code = AiProviderErrorCode::Unauthorized;
    else if (status == 429)
        code = AiProviderErrorCode::RateLimited;
    else if (status >= 500)
        code = AiProviderErrorCode::ServerError;

    const ApiErrorDetails details = apiErrorDetails(body);
    AiProviderError error = providerResponseError(
        code, m_providerName, userMessageFor(code),
        QStringLiteral("HTTP %1 api_code=%2 message=%3")
            .arg(status).arg(details.code, details.message));
    error.httpStatus = status;
    error.apiCode = details.code;
    return error;
}

QUrl providerEndpoint(const QUrl& baseUrl, const QString& relativePath)
{
    QUrl url = baseUrl;
    QString path = url.path();
    if (!path.endsWith(QLatin1Char('/')))
        path.append(QLatin1Char('/'));
    QString relative = relativePath;
    while (relative.startsWith(QLatin1Char('/')))
        relative.remove(0, 1);
    path.append(relative);
    url.setPath(path);
    return url;
}

AiProviderError providerResponseError(AiProviderErrorCode code,
                                      const QString& provider,
                                      const QString& userMessage,
                                      const QString& diagnostic)
{
    AiProviderError error;
    error.code = code;
    error.provider = provider;
    error.userMessage = userMessage;
    error.diagnostic = diagnostic;
    return error;
}
