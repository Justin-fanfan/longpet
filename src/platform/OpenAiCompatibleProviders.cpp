#include "OpenAiCompatibleProviders.h"

#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QSet>

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
                           bool stream,
                           const QList<AiToolDefinition>& tools = {})
{
    QJsonArray messageArray;
    for (const AiChatMessage& message : messages) {
        QJsonObject messageObject {
            {QStringLiteral("role"), message.role},
            {QStringLiteral("content"), message.content}
        };
        if (!message.name.isEmpty())
            messageObject.insert(QStringLiteral("name"), message.name);
        if (!message.toolCallId.isEmpty())
            messageObject.insert(QStringLiteral("tool_call_id"), message.toolCallId);
        if (!message.toolCalls.isEmpty()) {
            QJsonArray toolCallArray;
            for (const AiToolCall& call : message.toolCalls) {
                toolCallArray.append(QJsonObject {
                    {QStringLiteral("id"), call.id},
                    {QStringLiteral("type"), QStringLiteral("function")},
                    {QStringLiteral("function"), QJsonObject {
                        {QStringLiteral("name"), call.name},
                        {QStringLiteral("arguments"), call.argumentsJson}
                    }}
                });
            }
            messageObject.insert(QStringLiteral("tool_calls"), toolCallArray);
        }
        messageArray.append(messageObject);
    }
    QJsonObject body {
        {QStringLiteral("model"), configuration.model},
        {QStringLiteral("messages"), messageArray},
        {QStringLiteral("stream"), stream}
    };
    if (!tools.isEmpty()) {
        QJsonArray toolArray;
        for (const AiToolDefinition& tool : tools) {
            toolArray.append(QJsonObject {
                {QStringLiteral("type"), QStringLiteral("function")},
                {QStringLiteral("function"), QJsonObject {
                    {QStringLiteral("name"), tool.name},
                    {QStringLiteral("description"), tool.description},
                    {QStringLiteral("parameters"), tool.parameters}
                }}
            });
        }
        body.insert(QStringLiteral("tools"), toolArray);
        body.insert(QStringLiteral("tool_choice"), QStringLiteral("auto"));
    }
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

QList<AiToolCall> parseToolCalls(const QJsonArray& array)
{
    QList<AiToolCall> calls;
    for (const QJsonValue& value : array) {
        const QJsonObject object = value.toObject();
        const QJsonObject function = object.value(QStringLiteral("function")).toObject();
        AiToolCall call;
        call.id = object.value(QStringLiteral("id")).toString();
        call.name = function.value(QStringLiteral("name")).toString();
        call.argumentsJson = function.value(QStringLiteral("arguments")).toString();
        if (object.value(QStringLiteral("type")).toString() != QStringLiteral("function"))
            call.name.clear();
        calls.append(call);
    }
    return calls;
}

bool validToolCalls(const QList<AiToolCall>& calls)
{
    if (calls.size() > 8)
        return false;
    QSet<QString> ids;
    for (const auto& call : calls) {
        const QJsonDocument arguments = QJsonDocument::fromJson(call.argumentsJson.toUtf8());
        if (call.id.isEmpty() || call.id.size() > 128 || ids.contains(call.id)
            || call.name.isEmpty() || call.name.size() > 64
            || call.argumentsJson.size() > 32 * 1024 || !arguments.isObject())
            return false;
        ids.insert(call.id);
    }
    return true;
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
            this, [this](quint64 sessionId, const AiProviderError& error) {
        resetStream();
        emit requestFailed(sessionId, error);
    });
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

void OpenAiCompatibleLlmProvider::completeChatWithTools(
    quint64 sessionId, const QList<AiChatMessage>& messages,
    const QList<AiToolDefinition>& tools)
{
    resetStream();
    m_http.postJson(sessionId, providerEndpoint(
        m_configuration.apiBaseUrl, QStringLiteral("chat/completions")),
        m_configuration.apiKey,
        chatRequestBody(m_configuration, messages, false, tools));
}

void OpenAiCompatibleLlmProvider::streamChatWithTools(
    quint64 sessionId, const QList<AiChatMessage>& messages,
    const QList<AiToolDefinition>& tools)
{
    resetStream();
    m_streamSessionId = sessionId;
    m_http.postJsonStream(sessionId, providerEndpoint(
        m_configuration.apiBaseUrl, QStringLiteral("chat/completions")),
        m_configuration.apiKey,
        chatRequestBody(m_configuration, messages, true, tools));
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
        const QList<AiToolCall> toolCalls = m_streamToolCalls.values();
        const bool endedNormally = m_streamDone || m_streamFinishReasonSeen;
        const bool validFinish = m_streamFinishReason.isEmpty()
            || m_streamFinishReason == QStringLiteral("stop")
            || m_streamFinishReason == QStringLiteral("tool_calls");
        if (!endedNormally || !validFinish || !validToolCalls(toolCalls)
            || (!toolCalls.isEmpty() && m_streamFinishReason != QStringLiteral("tool_calls"))
            || (toolCalls.isEmpty() && m_streamFinishReason == QStringLiteral("tool_calls"))) {
            resetStream();
            emit requestFailed(sessionId, invalidResponse(
                QStringLiteral("openai-compatible/llm"),
                QStringLiteral("SSE incomplete stream, invalid tool calls or finish_reason")));
            return;
        }
        if (responseText.isEmpty() && toolCalls.isEmpty()) {
            resetStream();
            emit requestFailed(sessionId, invalidResponse(
                QStringLiteral("openai-compatible/llm"),
                QStringLiteral("SSE stream has no content"),
                AiProviderErrorCode::EmptyResult));
            return;
        }
        resetStream();
        if (!toolCalls.isEmpty()) {
            emit toolCallsReady(sessionId, responseText, toolCalls);
            return;
        }
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
    const QJsonObject message = choices.isEmpty() ? QJsonObject()
        : choices.first().toObject().value(QStringLiteral("message")).toObject();
    const QString content = message.value(QStringLiteral("content")).toString().trimmed();
    const QList<AiToolCall> toolCalls = parseToolCalls(
        message.value(QStringLiteral("tool_calls")).toArray());
    const QString finishReason = choices.isEmpty() ? QString() : choices.first().toObject()
        .value(QStringLiteral("finish_reason")).toString();
    if (!validToolCalls(toolCalls)
        || (message.contains(QStringLiteral("tool_calls"))
            && !message.value(QStringLiteral("tool_calls")).isNull()
            && !message.value(QStringLiteral("tool_calls")).isArray())
        || (!finishReason.isEmpty() && finishReason != QStringLiteral("stop")
            && finishReason != QStringLiteral("tool_calls"))) {
        emit requestFailed(sessionId, invalidResponse(QStringLiteral("openai-compatible/llm"),
                                                      QStringLiteral("malformed tool call or finish reason")));
        return;
    }
    if (content.isEmpty() && toolCalls.isEmpty()) {
        emit requestFailed(sessionId, invalidResponse(
            QStringLiteral("openai-compatible/llm"),
            QStringLiteral("response has no choices[0].message.content"),
            AiProviderErrorCode::EmptyResult));
        return;
    }
    if (!toolCalls.isEmpty()) {
        emit toolCallsReady(sessionId, content, toolCalls);
        return;
    }
    emit chatCompletionReady(sessionId, content);
}

void OpenAiCompatibleLlmProvider::handleStreamChunk(
    quint64 sessionId, const QByteArray& chunk)
{
    if (sessionId != m_streamSessionId)
        return;
    m_streamBytesReceived += chunk.size();
    if (m_streamBytesReceived > 1024 * 1024) {
        m_http.cancel(sessionId);
        resetStream();
        emit requestFailed(sessionId, invalidResponse(QStringLiteral("openai-compatible/llm"),
                                                      QStringLiteral("SSE byte budget exceeded")));
        return;
    }
    processStreamEvents(sessionId, m_sseParser.append(chunk));
}

bool OpenAiCompatibleLlmProvider::processStreamEvents(
    quint64 sessionId, const QList<QByteArray>& events)
{
    auto reject = [this, sessionId](const QString& diagnostic) {
        m_http.cancel(sessionId);
        resetStream();
        emit requestFailed(sessionId, invalidResponse(QStringLiteral("openai-compatible/llm"), diagnostic));
        return false;
    };
    for (const QByteArray& event : events) {
        if (m_streamDone)
            continue;
        if (event.trimmed() == QByteArrayLiteral("[DONE]")) {
            m_streamDone = true;
            continue;
        }

        QJsonParseError parseError;
        const QJsonObject root = parseObject(event, &parseError);
        if (parseError.error != QJsonParseError::NoError || root.isEmpty()) {
            const QString diagnostic = QStringLiteral("SSE JSON error: %1")
                .arg(parseError.errorString());
            m_http.cancel(sessionId);
            resetStream();
            emit requestFailed(sessionId, invalidResponse(
                QStringLiteral("openai-compatible/llm"), diagnostic));
            return false;
        }

        const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
        if (root.contains(QStringLiteral("error")))
            return reject(QStringLiteral("SSE provider error event"));
        if (choices.isEmpty())
            continue; // Some providers send a final usage-only SSE event.
        const QJsonObject choice = choices.first().toObject();
        if (!choice.value(QStringLiteral("finish_reason")).isNull()
            && !choice.value(QStringLiteral("finish_reason")).isUndefined()) {
            if (!choice.value(QStringLiteral("finish_reason")).isString())
                return reject(QStringLiteral("finish_reason is not a string"));
            m_streamFinishReasonSeen = true;
            m_streamFinishReason = choice.value(QStringLiteral("finish_reason")).toString();
        }
        const QJsonObject deltaObject = choice.value(QStringLiteral("delta")).toObject();
        if (deltaObject.contains(QStringLiteral("tool_calls"))
            && !deltaObject.value(QStringLiteral("tool_calls")).isArray()
            && !deltaObject.value(QStringLiteral("tool_calls")).isNull())
            return reject(QStringLiteral("tool_calls is not an array"));
        const QJsonArray toolCallDeltas = deltaObject.value(
            QStringLiteral("tool_calls")).toArray();
        for (const QJsonValue& value : toolCallDeltas) {
            const QJsonObject object = value.toObject();
            const QJsonValue indexValue = object.value(QStringLiteral("index"));
            const int index = indexValue.toInt(-1);
            if (!indexValue.isDouble() || indexValue.toDouble() != index || index < 0 || index >= 8
                || (object.contains(QStringLiteral("type"))
                    && object.value(QStringLiteral("type")).toString() != QStringLiteral("function")))
                return reject(QStringLiteral("invalid tool delta index or type"));
            AiToolCall& call = m_streamToolCalls[index];
            const QString id = object.value(QStringLiteral("id")).toString();
            if (!call.id.isEmpty() && !id.isEmpty() && call.id != id)
                return reject(QStringLiteral("tool delta changed id"));
            if (!id.isEmpty())
                call.id = id;
            const QJsonObject function = object.value(QStringLiteral("function")).toObject();
            call.name.append(function.value(QStringLiteral("name")).toString());
            call.argumentsJson.append(function.value(QStringLiteral("arguments")).toString());
            if (call.id.size() > 128 || call.name.size() > 64 || call.argumentsJson.size() > 32 * 1024)
                return reject(QStringLiteral("tool delta size exceeded"));
        }
        const QString delta = deltaObject.value(QStringLiteral("content")).toString();
        if (delta.isEmpty())
            continue;
        m_streamText.append(delta);
        if (m_streamText.size() > 64 * 1024)
            return reject(QStringLiteral("stream text size exceeded"));
        emit chatDelta(sessionId, delta);
    }
    return true;
}

void OpenAiCompatibleLlmProvider::resetStream()
{
    m_sseParser.reset();
    m_streamSessionId = 0;
    m_streamBytesReceived = 0;
    m_streamText.clear();
    m_streamDone = false;
    m_streamFinishReasonSeen = false;
    m_streamFinishReason.clear();
    m_streamToolCalls.clear();
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
