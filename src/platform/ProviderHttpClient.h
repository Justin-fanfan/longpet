#pragma once

#include "model/AiModels.h"

#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QTimer>

class QHttpMultiPart;
class QNetworkReply;
class QNetworkRequest;

struct ProviderHttpResponse {
    int statusCode = 0;
    QByteArray contentType;
    QByteArray body;
};

Q_DECLARE_METATYPE(ProviderHttpResponse)

class ProviderHttpClient final : public QObject {
    Q_OBJECT

public:
    using Headers = QList<QPair<QByteArray, QByteArray>>;

    ProviderHttpClient(QString providerName, int timeoutMs,
                       QObject* parent = nullptr);

    void postJson(quint64 sessionId, const QUrl& url, const QString& apiKey,
                  const QByteArray& json, const Headers& extraHeaders = {});
    void postJsonStream(quint64 sessionId, const QUrl& url,
                        const QString& apiKey, const QByteArray& json,
                        const Headers& extraHeaders = {});
    void postMultipart(quint64 sessionId, const QUrl& url, const QString& apiKey,
                       QHttpMultiPart* multipart,
                       const Headers& extraHeaders = {});
    void getBinary(quint64 sessionId, const QUrl& url,
                   const Headers& extraHeaders = {});
    void cancel(quint64 sessionId);

signals:
    void streamChunkReceived(quint64 sessionId, const QByteArray& chunk);
    void succeeded(quint64 sessionId, const ProviderHttpResponse& response);
    void failed(quint64 sessionId, const AiProviderError& error);

private:
    QNetworkRequest requestFor(const QUrl& url, const QString& apiKey,
                               const QByteArray& accept,
                               const Headers& extraHeaders) const;
    void watch(QNetworkReply* reply, quint64 sessionId, bool streaming = false);
    void handleFinished(QNetworkReply* reply);
    AiProviderError httpFailure(int status, const QByteArray& body) const;

    QString m_providerName;
    int m_timeoutMs = 30'000;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QTimer m_timeout;
    quint64 m_sessionId = 0;
    bool m_timedOut = false;
    bool m_streaming = false;
    QByteArray m_responseBody;
};

QUrl providerEndpoint(const QUrl& baseUrl, const QString& relativePath);
AiProviderError providerResponseError(AiProviderErrorCode code,
                                      const QString& provider,
                                      const QString& userMessage,
                                      const QString& diagnostic);
