#include "OpenAiCompatibleProvider.h"

#include <QCoreApplication>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace {
QHttpPart textPart(const QByteArray& name, const QByteArray& value)
{
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QStringLiteral("form-data; name=\"%1\"")
                       .arg(QString::fromLatin1(name)));
    part.setBody(value);
    return part;
}

QString serverErrorMessage(const QByteArray& body)
{
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (!document.isObject())
        return QString::fromUtf8(body.left(300)).trimmed();
    const QJsonValue error = document.object().value(QStringLiteral("error"));
    if (error.isString())
        return error.toString().trimmed();
    if (error.isObject())
        return error.toObject().value(QStringLiteral("message")).toString().trimmed();
    return document.object().value(QStringLiteral("message")).toString().trimmed();
}
}

OpenAiCompatibleProvider::OpenAiCompatibleProvider(
    const AiConfiguration& configuration, QObject* parent)
    : AiProviderPort(parent), m_configuration(configuration)
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(qMax(1'000, configuration.requestTimeoutMs));
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (!m_reply)
            return;
        m_timedOut = true;
        m_reply->abort();
    });
}

void OpenAiCompatibleProvider::transcribe(quint64 sessionId,
                                           const QByteArray& wavAudio)
{
    if (m_reply)
        cancel(m_sessionId);
    auto* multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    multipart->append(textPart(QByteArrayLiteral("model"),
                               m_configuration.asrModel.toUtf8()));
    multipart->append(textPart(QByteArrayLiteral("response_format"),
                               QByteArrayLiteral("json")));
    if (!m_configuration.language.isEmpty()) {
        multipart->append(textPart(QByteArrayLiteral("language"),
                                   m_configuration.language.toUtf8()));
    }
    QHttpPart audioPart;
    audioPart.setHeader(QNetworkRequest::ContentTypeHeader,
                        QStringLiteral("audio/wav"));
    audioPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                        QStringLiteral("form-data; name=\"file\"; filename=\"recording.wav\""));
    audioPart.setBody(wavAudio);
    multipart->append(audioPart);

    QNetworkReply* reply = m_network.post(
        requestFor(QStringLiteral("audio/transcriptions")), multipart);
    multipart->setParent(reply);
    watchReply(reply, sessionId, AiRequestStage::Asr);
}

void OpenAiCompatibleProvider::completeChat(
    quint64 sessionId, const QList<AiChatMessage>& messages)
{
    if (m_reply)
        cancel(m_sessionId);
    QJsonArray messageArray;
    for (const AiChatMessage& message : messages) {
        messageArray.append(QJsonObject {
            {QStringLiteral("role"), message.role},
            {QStringLiteral("content"), message.content}
        });
    }
    const QJsonObject body {
        {QStringLiteral("model"), m_configuration.llmModel},
        {QStringLiteral("messages"), messageArray},
        {QStringLiteral("stream"), false}
    };
    QNetworkRequest request = requestFor(QStringLiteral("chat/completions"));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    watchReply(m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact)),
               sessionId, AiRequestStage::Llm);
}

void OpenAiCompatibleProvider::synthesize(quint64 sessionId,
                                          const QString& text)
{
    if (m_reply)
        cancel(m_sessionId);
    const QJsonObject body {
        {QStringLiteral("model"), m_configuration.ttsModel},
        {QStringLiteral("voice"), m_configuration.ttsVoice},
        {QStringLiteral("input"), text},
        {QStringLiteral("response_format"), QStringLiteral("wav")}
    };
    QNetworkRequest request = requestFor(QStringLiteral("audio/speech"));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("audio/wav"));
    watchReply(m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact)),
               sessionId, AiRequestStage::Tts);
}

void OpenAiCompatibleProvider::cancel(quint64 sessionId)
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

QUrl OpenAiCompatibleProvider::endpoint(const QString& relativePath) const
{
    QUrl url = m_configuration.apiBaseUrl;
    QString path = url.path();
    if (!path.endsWith(QLatin1Char('/')))
        path.append(QLatin1Char('/'));
    path.append(relativePath);
    url.setPath(path);
    return url;
}

QNetworkRequest OpenAiCompatibleProvider::requestFor(
    const QString& relativePath) const
{
    QNetworkRequest request(endpoint(relativePath));
    request.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json"));
    const QByteArray applicationVersion = QCoreApplication::applicationVersion().toUtf8();
    request.setRawHeader(QByteArrayLiteral("User-Agent"),
                         QByteArrayLiteral("LongPet/")
                             + (applicationVersion.isEmpty()
                                    ? QByteArrayLiteral("unknown") : applicationVersion));
    if (!m_configuration.apiKey.isEmpty()) {
        request.setRawHeader(QByteArrayLiteral("Authorization"),
                             QByteArrayLiteral("Bearer ")
                                 + m_configuration.apiKey.toUtf8());
    }
    return request;
}

void OpenAiCompatibleProvider::watchReply(QNetworkReply* reply,
                                          quint64 sessionId,
                                          AiRequestStage stage)
{
    m_reply = reply;
    m_sessionId = sessionId;
    m_stage = stage;
    m_timedOut = false;
    m_timeout.start();
    connect(reply, &QNetworkReply::finished,
            this, [this, reply] { handleFinished(reply); });
}

void OpenAiCompatibleProvider::handleFinished(QNetworkReply* reply)
{
    if (reply != m_reply)
        return;
    m_timeout.stop();
    const quint64 sessionId = m_sessionId;
    const AiRequestStage stage = m_stage;
    const bool timedOut = m_timedOut;
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkErrorText = reply->errorString();
    const int status = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray contentType = reply->rawHeader(
        QByteArrayLiteral("Content-Type")).toLower();
    const QByteArray body = reply->isOpen() ? reply->readAll() : QByteArray();
    m_reply.clear();
    m_timedOut = false;
    reply->deleteLater();

    if (timedOut) {
        emitFailure(sessionId, stage, QStringLiteral("请求超时，请稍后再试"),
                    QStringLiteral("%1 request timed out after %2 ms")
                        .arg(stageName(stage)).arg(m_configuration.requestTimeoutMs));
        return;
    }
    if (status > 0 && (status < 200 || status >= 300)) {
        const QString detail = serverErrorMessage(body);
        emitFailure(sessionId, stage,
                    QStringLiteral("%1服务暂时不可用").arg(stageName(stage)),
                    QStringLiteral("HTTP %1: %2").arg(status).arg(detail));
        return;
    }
    if (networkError != QNetworkReply::NoError) {
        emitFailure(sessionId, stage,
                    QStringLiteral("网络连接失败，请检查 Wi-Fi"),
                    QStringLiteral("%1: %2").arg(stageName(stage), networkErrorText));
        return;
    }

    if (stage == AiRequestStage::Tts) {
        if (contentType.contains("application/json")) {
            emitFailure(sessionId, stage,
                        QStringLiteral("语音生成服务暂时不可用"),
                        QStringLiteral("TTS returned JSON: %1")
                            .arg(serverErrorMessage(body)));
        } else if (body.isEmpty()) {
            emitFailure(sessionId, stage, QStringLiteral("语音生成失败，请稍后再试"),
                        QStringLiteral("TTS response body is empty"));
        } else {
            emit speechReady(sessionId, body);
        }
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emitFailure(sessionId, stage,
                    QStringLiteral("服务返回内容无法读取，请稍后再试"),
                    QStringLiteral("%1 JSON error: %2")
                        .arg(stageName(stage), parseError.errorString()));
        return;
    }

    if (stage == AiRequestStage::Asr) {
        const QString text = document.object().value(QStringLiteral("text"))
                                 .toString().trimmed();
        if (text.isEmpty()) {
            emitFailure(sessionId, stage, QStringLiteral("没有听清，请再说一次"),
                        QStringLiteral("ASR response has no text"));
        } else {
            emit transcriptionReady(sessionId, text);
        }
        return;
    }

    const QJsonArray choices = document.object().value(
        QStringLiteral("choices")).toArray();
    const QString content = choices.isEmpty() ? QString()
        : choices.first().toObject().value(QStringLiteral("message"))
              .toObject().value(QStringLiteral("content")).toString().trimmed();
    if (content.isEmpty()) {
        emitFailure(sessionId, stage, QStringLiteral("暂时没有得到回答，请稍后再试"),
                    QStringLiteral("LLM response has no choices[0].message.content"));
    } else {
        emit chatCompletionReady(sessionId, content);
    }
}

void OpenAiCompatibleProvider::emitFailure(
    quint64 sessionId, AiRequestStage stage, const QString& message,
    const QString& diagnostic)
{
    emit requestFailed(sessionId, stage, message, diagnostic);
}

QString OpenAiCompatibleProvider::stageName(AiRequestStage stage) const
{
    switch (stage) {
    case AiRequestStage::Asr: return QStringLiteral("语音识别");
    case AiRequestStage::Llm: return QStringLiteral("AI 对话");
    case AiRequestStage::Tts: return QStringLiteral("语音生成");
    }
    return QStringLiteral("AI");
}
