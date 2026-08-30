#include "OpenAiCompatibleProviders.h"

#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

AiProviderError invalidResponse(const QString& provider,
                                const QString& diagnostic,
                                AiProviderErrorCode code = AiProviderErrorCode::InvalidResponse)
{
    QString message = QStringLiteral("AI 服务返回内容异常，请稍后再试");
    if (code == AiProviderErrorCode::EmptyResult)
        message = QStringLiteral("AI 服务没有返回有效结果");
    return providerResponseError(code, provider, message, diagnostic);
}

QJsonObject parseObject(const QByteArray& body, QJsonParseError* error)
{
    const QJsonDocument document = QJsonDocument::fromJson(body, error);
    return document.isObject() ? document.object() : QJsonObject();
}

QByteArray chatRequestBody(const LlmProviderConfiguration& configuration,
                           const QList<AiChatMessage>& messages,
                           bool stream)
{
    QJsonArray messageArray;
    for (const AiChatMessage& message : messages) {
        messageArray.append(QJsonObject {
            {QStringLiteral("role"), message.role},
            {QStringLiteral("content"), message.content}
        });
    }
    return QJsonDocument(QJsonObject {
        {QStringLiteral("model"), configuration.model},
        {QStringLiteral("messages"), messageArray},
        {QStringLiteral("stream"), stream}
    }).toJson(QJsonDocument::Compact);
}
}

OpenAiAsrProvider::OpenAiAsrProvider(
    const AsrProviderConfiguration& configuration, int timeoutMs, QObject* parent)
    : AsrProviderPort(parent),
      m_configuration(configuration),
      m_http(QStringLiteral("openai-compatible/asr"), timeoutMs)
{
    connect(&m_http, &ProviderHttpClient::succeeded,
            this, &OpenAiAsrProvider::handleResponse);
    connect(&m_http, &ProviderHttpClient::failed,
            this, &AsrProviderPort::requestFailed);
}

void OpenAiAsrProvider::transcribe(quint64 sessionId,
                                   const QByteArray& wavAudio)
{
    auto* multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    multipart->append(textPart(QByteArrayLiteral("model"),
                               m_configuration.model.toUtf8()));
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
    m_http.postMultipart(sessionId, providerEndpoint(
        m_configuration.apiBaseUrl, QStringLiteral("audio/transcriptions")),
        m_configuration.apiKey, multipart);
}

void OpenAiAsrProvider::cancel(quint64 sessionId)
{
    m_http.cancel(sessionId);
}

void OpenAiAsrProvider::handleResponse(
    quint64 sessionId, const ProviderHttpResponse& response)
{
    QJsonParseError parseError;
    const QJsonObject root = parseObject(response.body, &parseError);
    if (parseError.error != QJsonParseError::NoError || root.isEmpty()) {
        emit requestFailed(sessionId, invalidResponse(
            QStringLiteral("openai-compatible/asr"),
            QStringLiteral("JSON error: %1").arg(parseError.errorString())));
        return;
    }
    const QString text = root.value(QStringLiteral("text")).toString().trimmed();
    if (text.isEmpty()) {
        emit requestFailed(sessionId, invalidResponse(
            QStringLiteral("openai-compatible/asr"),
            QStringLiteral("response has no text"), AiProviderErrorCode::EmptyResult));
        return;
    }
    emit transcriptionReady(sessionId, text);
}

OpenAiCompatibleLlmProvider::OpenAiCompatibleLlmProvider(
    const LlmProviderConfiguration& configuration, int timeoutMs, QObject* parent)
    : LlmProviderPort(parent),
      m_configuration(configuration),
      m_http(QStringLiteral("openai-compatible/llm"), timeoutMs)
{
    connect(&m_http, &ProviderHttpClient::succeeded,
            this, &OpenAiCompatibleLlmProvider::handleResponse);
    connect(&m_http, &ProviderHttpClient::failed,
            this, &LlmProviderPort::requestFailed);
    connect(&m_http, &ProviderHttpClient::streamChunkReceived,
            this, &OpenAiCompatibleLlmProvider::handleStreamChunk);
}

void OpenAiCompatibleLlmProvider::completeChat(
    quint64 sessionId, const QList<AiChatMessage>& messages)
{
    resetStream();
    m_http.postJson(sessionId, providerEndpoint(
        m_configuration.apiBaseUrl, QStringLiteral("chat/completions")),
        m_configuration.apiKey,
        chatRequestBody(m_configuration, messages, false));
}

void OpenAiCompatibleLlmProvider::streamChat(
    quint64 sessionId, const QList<AiChatMessage>& messages)
{
    resetStream();
    m_streamSessionId = sessionId;
    m_http.postJsonStream(sessionId, providerEndpoint(
        m_configuration.apiBaseUrl, QStringLiteral("chat/completions")),
        m_configuration.apiKey,
        chatRequestBody(m_configuration, messages, true));
}

void OpenAiCompatibleLlmProvider::cancel(quint64 sessionId)
{
    m_http.cancel(sessionId);
    if (sessionId == m_streamSessionId)
        resetStream();
}

void OpenAiCompatibleLlmProvider::handleResponse(
    quint64 sessionId, const ProviderHttpResponse& response)
{
    if (sessionId == m_streamSessionId) {
        if (!processStreamEvents(sessionId, m_sseParser.finish()))
            return;
        const QString responseText = m_streamText.trimmed();
        const bool endedNormally = m_streamDone || m_streamFinishReasonSeen;
        if (!endedNormally) {
            resetStream();
            emit requestFailed(sessionId, invalidResponse(
                QStringLiteral("openai-compatible/llm"),
                QStringLiteral("SSE stream ended without [DONE] or finish_reason")));
            return;
        }
        if (responseText.isEmpty()) {
            resetStream();
            emit requestFailed(sessionId, invalidResponse(
                QStringLiteral("openai-compatible/llm"),
                QStringLiteral("SSE stream has no content"),
                AiProviderErrorCode::EmptyResult));
            return;
        }
        resetStream();
        emit chatCompletionReady(sessionId, responseText);
        return;
    }

    QJsonParseError parseError;
    const QJsonObject root = parseObject(response.body, &parseError);
    if (parseError.error != QJsonParseError::NoError || root.isEmpty()) {
        emit requestFailed(sessionId, invalidResponse(
            QStringLiteral("openai-compatible/llm"),
            QStringLiteral("JSON error: %1").arg(parseError.errorString())));
        return;
    }
    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    const QString content = choices.isEmpty() ? QString()
        : choices.first().toObject().value(QStringLiteral("message"))
              .toObject().value(QStringLiteral("content")).toString().trimmed();
    if (content.isEmpty()) {
        emit requestFailed(sessionId, invalidResponse(
            QStringLiteral("openai-compatible/llm"),
            QStringLiteral("response has no choices[0].message.content"),
            AiProviderErrorCode::EmptyResult));
        return;
    }
    emit chatCompletionReady(sessionId, content);
}

void OpenAiCompatibleLlmProvider::handleStreamChunk(
    quint64 sessionId, const QByteArray& chunk)
{
    if (sessionId != m_streamSessionId)
        return;
    processStreamEvents(sessionId, m_sseParser.append(chunk));
}

bool OpenAiCompatibleLlmProvider::processStreamEvents(
    quint64 sessionId, const QList<QByteArray>& events)
{
    for (const QByteArray& event : events) {
        if (event.trimmed() == QByteArrayLiteral("[DONE]")) {
            m_streamDone = true;
            continue;
        }

        QJsonParseError parseError;
        const QJsonObject root = parseObject(event, &parseError);
        if (parseError.error != QJsonParseError::NoError || root.isEmpty()) {
            const QString diagnostic = QStringLiteral("SSE JSON error: %1 payload=%2")
                .arg(parseError.errorString(),
                     QString::fromUtf8(event.left(160)));
            m_http.cancel(sessionId);
            resetStream();
            emit requestFailed(sessionId, invalidResponse(
                QStringLiteral("openai-compatible/llm"), diagnostic));
            return false;
        }

        const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
        if (choices.isEmpty())
            continue; // Some providers send a final usage-only SSE event.
        const QJsonObject choice = choices.first().toObject();
        if (!choice.value(QStringLiteral("finish_reason")).isNull()
            && !choice.value(QStringLiteral("finish_reason")).isUndefined()) {
            m_streamFinishReasonSeen = true;
        }
        const QString delta = choice.value(QStringLiteral("delta"))
            .toObject().value(QStringLiteral("content")).toString();
        if (delta.isEmpty())
            continue;
        m_streamText.append(delta);
        emit chatDelta(sessionId, delta);
    }
    return true;
}

void OpenAiCompatibleLlmProvider::resetStream()
{
    m_sseParser.reset();
    m_streamSessionId = 0;
    m_streamText.clear();
    m_streamDone = false;
    m_streamFinishReasonSeen = false;
}

OpenAiTtsProvider::OpenAiTtsProvider(
    const TtsProviderConfiguration& configuration, int timeoutMs, QObject* parent)
    : TtsProviderPort(parent),
      m_configuration(configuration),
      m_http(QStringLiteral("openai-compatible/tts"), timeoutMs)
{
    connect(&m_http, &ProviderHttpClient::succeeded,
            this, &OpenAiTtsProvider::handleResponse);
    connect(&m_http, &ProviderHttpClient::failed,
            this, &TtsProviderPort::requestFailed);
}

void OpenAiTtsProvider::synthesize(quint64 sessionId, const QString& text)
{
    const QJsonObject body {
        {QStringLiteral("model"), m_configuration.model},
        {QStringLiteral("voice"), m_configuration.voice},
        {QStringLiteral("input"), text},
        {QStringLiteral("response_format"), QStringLiteral("wav")}
    };
    m_http.postJson(sessionId, providerEndpoint(
        m_configuration.apiBaseUrl, QStringLiteral("audio/speech")),
        m_configuration.apiKey, QJsonDocument(body).toJson(QJsonDocument::Compact),
        {{QByteArrayLiteral("Accept"), QByteArrayLiteral("audio/wav")}});
}

void OpenAiTtsProvider::cancel(quint64 sessionId)
{
    m_http.cancel(sessionId);
}

void OpenAiTtsProvider::handleResponse(
    quint64 sessionId, const ProviderHttpResponse& response)
{
    if (response.contentType.contains("application/json")) {
        emit requestFailed(sessionId, invalidResponse(
            QStringLiteral("openai-compatible/tts"),
            QStringLiteral("TTS returned JSON instead of audio")));
        return;
    }
    if (response.body.isEmpty()) {
        emit requestFailed(sessionId, invalidResponse(
            QStringLiteral("openai-compatible/tts"),
            QStringLiteral("audio response is empty"),
            AiProviderErrorCode::EmptyResult));
        return;
    }
    emit speechReady(sessionId, response.body);
}
