#include "AliyunProviders.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
const QString AliyunGenerationPath =
    QStringLiteral("services/aigc/multimodal-generation/generation");
const QString AliyunSpeechSynthesizerPath =
    QStringLiteral("services/audio/tts/SpeechSynthesizer");

AiProviderError aliyunResponseError(AiProviderErrorCode code,
                                    const QString& capability,
                                    const QString& diagnostic)
{
    QString userMessage = QStringLiteral("AI 服务返回内容异常，请稍后再试");
    if (code == AiProviderErrorCode::EmptyResult)
        userMessage = QStringLiteral("AI 服务没有返回有效结果");
    return providerResponseError(
        code, QStringLiteral("aliyun/%1").arg(capability), userMessage, diagnostic);
}

QString contentText(const QJsonValue& content)
{
    if (content.isString())
        return content.toString().trimmed();
    if (!content.isArray())
        return {};
    QStringList parts;
    for (const QJsonValue& value : content.toArray()) {
        const QString text = value.toObject().value(
            QStringLiteral("text")).toString().trimmed();
        if (!text.isEmpty())
            parts.append(text);
    }
    return parts.join(QString()).trimmed();
}
}

AliyunAsrProvider::AliyunAsrProvider(
    const AsrProviderConfiguration& configuration, int timeoutMs, QObject* parent)
    : AsrProviderPort(parent),
      m_configuration(configuration),
      m_http(QStringLiteral("aliyun/asr"), timeoutMs)
{
    connect(&m_http, &ProviderHttpClient::succeeded,
            this, &AliyunAsrProvider::handleResponse);
    connect(&m_http, &ProviderHttpClient::failed,
            this, &AsrProviderPort::requestFailed);
}

void AliyunAsrProvider::transcribe(quint64 sessionId,
                                   const QByteArray& wavAudio)
{
    const QString dataUri = QStringLiteral("data:audio/wav;base64,")
        + QString::fromLatin1(wavAudio.toBase64());
    QJsonObject content;
    QJsonObject parameters;
    if (usesCurrentAudioSchema()) {
        content = QJsonObject {
            {QStringLiteral("type"), QStringLiteral("input_audio")},
            {QStringLiteral("input_audio"), QJsonObject {
                {QStringLiteral("data"), dataUri}
            }}
        };
        parameters.insert(QStringLiteral("format"), QStringLiteral("wav"));
        parameters.insert(QStringLiteral("sample_rate"), QStringLiteral("16000"));
        if (!m_configuration.language.trimmed().isEmpty()) {
            parameters.insert(QStringLiteral("language_hints"),
                              QJsonArray {m_configuration.language.trimmed()});
        }
    } else {
        content = QJsonObject {{QStringLiteral("audio"), dataUri}};
        QJsonObject asrOptions {{QStringLiteral("enable_itn"), false}};
        if (!m_configuration.language.trimmed().isEmpty()) {
            asrOptions.insert(QStringLiteral("language"),
                              m_configuration.language.trimmed());
        }
        parameters.insert(QStringLiteral("asr_options"), asrOptions);
    }

    const QJsonObject body {
        {QStringLiteral("model"), m_configuration.model},
        {QStringLiteral("input"), QJsonObject {
            {QStringLiteral("messages"), QJsonArray {QJsonObject {
                {QStringLiteral("role"), QStringLiteral("user")},
                {QStringLiteral("content"), QJsonArray {content}}
            }}}
        }},
        {QStringLiteral("parameters"), parameters}
    };
    m_http.postJson(sessionId, providerEndpoint(
        m_configuration.apiBaseUrl, AliyunGenerationPath),
        m_configuration.apiKey, QJsonDocument(body).toJson(QJsonDocument::Compact),
        {{QByteArrayLiteral("X-DashScope-SSE"), QByteArrayLiteral("disable")}});
}

void AliyunAsrProvider::cancel(quint64 sessionId)
{
    m_http.cancel(sessionId);
}

void AliyunAsrProvider::handleResponse(
    quint64 sessionId, const ProviderHttpResponse& response)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(response.body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit requestFailed(sessionId, aliyunResponseError(
            AiProviderErrorCode::InvalidResponse, QStringLiteral("asr"),
            QStringLiteral("JSON error: %1").arg(parseError.errorString())));
        return;
    }
    const QJsonObject root = document.object();
    const QJsonObject output = root.value(QStringLiteral("output")).toObject();
    QString text = output.value(QStringLiteral("text")).toString().trimmed();
    if (text.isEmpty()) {
        const QJsonArray choices = output.value(QStringLiteral("choices")).toArray();
        if (!choices.isEmpty()) {
            text = contentText(choices.first().toObject()
                .value(QStringLiteral("message")).toObject()
                .value(QStringLiteral("content")));
        }
    }
    if (text.isEmpty()) {
        const QString apiCode = root.value(QStringLiteral("code")).toString();
        const QString message = root.value(QStringLiteral("message")).toString();
        AiProviderError error = aliyunResponseError(
            AiProviderErrorCode::EmptyResult, QStringLiteral("asr"),
            QStringLiteral("response has no transcript api_code=%1 message=%2")
                .arg(apiCode, message));
        error.apiCode = apiCode;
        emit requestFailed(sessionId, error);
        return;
    }
    emit transcriptionReady(sessionId, text);
}

bool AliyunAsrProvider::usesCurrentAudioSchema() const
{
    const QString model = m_configuration.model.trimmed().toLower();
    return model.startsWith(QStringLiteral("qwen-audio-"))
        || model.startsWith(QStringLiteral("fun-asr"));
}

AliyunTtsProvider::AliyunTtsProvider(
    const TtsProviderConfiguration& configuration, int timeoutMs, QObject* parent)
    : TtsProviderPort(parent),
      m_configuration(configuration),
      m_http(QStringLiteral("aliyun/tts"), timeoutMs)
{
    connect(&m_http, &ProviderHttpClient::succeeded,
            this, &AliyunTtsProvider::handleResponse);
    connect(&m_http, &ProviderHttpClient::failed,
            this, &TtsProviderPort::requestFailed);
}

void AliyunTtsProvider::synthesize(quint64 sessionId, const QString& text)
{
    m_phase = Phase::Generate;
    QJsonObject input {
        {QStringLiteral("text"), text},
        {QStringLiteral("voice"), m_configuration.voice}
    };
    QString endpointPath = AliyunGenerationPath;
    if (usesSpeechSynthesizerEndpoint()) {
        endpointPath = AliyunSpeechSynthesizerPath;
        input.insert(QStringLiteral("format"), QStringLiteral("wav"));
        input.insert(QStringLiteral("sample_rate"), 24'000);
    }
    const QJsonObject body {
        {QStringLiteral("model"), m_configuration.model},
        {QStringLiteral("input"), input}
    };
    m_http.postJson(sessionId, providerEndpoint(
        m_configuration.apiBaseUrl, endpointPath), m_configuration.apiKey,
        QJsonDocument(body).toJson(QJsonDocument::Compact));
}

void AliyunTtsProvider::cancel(quint64 sessionId)
{
    m_http.cancel(sessionId);
    m_phase = Phase::Generate;
}

void AliyunTtsProvider::handleResponse(
    quint64 sessionId, const ProviderHttpResponse& response)
{
    if (m_phase == Phase::Download) {
        m_phase = Phase::Generate;
        if (response.body.isEmpty()
            || response.contentType.contains("application/json")) {
            emit requestFailed(sessionId, aliyunResponseError(
                AiProviderErrorCode::EmptyResult, QStringLiteral("tts"),
                QStringLiteral("downloaded audio is empty or JSON")));
            return;
        }
        emit speechReady(sessionId, response.body);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(response.body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit requestFailed(sessionId, aliyunResponseError(
            AiProviderErrorCode::InvalidResponse, QStringLiteral("tts"),
            QStringLiteral("JSON error: %1").arg(parseError.errorString())));
        return;
    }
    const QJsonObject root = document.object();
    const QJsonObject audio = root.value(QStringLiteral("output")).toObject()
        .value(QStringLiteral("audio")).toObject();
    const QUrl audioUrl(audio.value(QStringLiteral("url")).toString());
    if (!audioUrl.isValid()
        || (audioUrl.scheme() != QStringLiteral("http")
            && audioUrl.scheme() != QStringLiteral("https"))) {
        const QString apiCode = root.value(QStringLiteral("code")).toString();
        const QString message = root.value(QStringLiteral("message")).toString();
        AiProviderError error = aliyunResponseError(
            AiProviderErrorCode::EmptyResult, QStringLiteral("tts"),
            QStringLiteral("response has no valid output.audio.url api_code=%1 message=%2")
                .arg(apiCode, message));
        error.apiCode = apiCode;
        emit requestFailed(sessionId, error);
        return;
    }
    m_phase = Phase::Download;
    // The signed OSS URL is already authorized. Never forward the DashScope key.
    m_http.getBinary(sessionId, audioUrl);
}

bool AliyunTtsProvider::usesSpeechSynthesizerEndpoint() const
{
    const QString model = m_configuration.model.trimmed().toLower();
    return model.startsWith(QStringLiteral("qwen-audio-"))
        || model.startsWith(QStringLiteral("cosyvoice"));
}
