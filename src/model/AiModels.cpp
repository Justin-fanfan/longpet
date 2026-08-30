#include "AiModels.h"

namespace {
QString endpointValidationError(const QString& capability,
                                const QString& provider,
                                const QUrl& baseUrl,
                                const QString& model)
{
    if (provider.trimmed().isEmpty())
        return QStringLiteral("%1 Provider 尚未配置").arg(capability);
    if (!baseUrl.isValid()
        || (baseUrl.scheme() != QStringLiteral("http")
            && baseUrl.scheme() != QStringLiteral("https"))) {
        return QStringLiteral("%1 API 地址未配置或格式不正确").arg(capability);
    }
    if (model.trimmed().isEmpty())
        return QStringLiteral("%1 模型尚未配置").arg(capability);
    return {};
}
}

QString AsrProviderConfiguration::validationError() const
{
    return endpointValidationError(QStringLiteral("ASR"), provider, apiBaseUrl, model);
}

QString LlmProviderConfiguration::validationError() const
{
    return endpointValidationError(QStringLiteral("LLM"), provider, apiBaseUrl, model);
}

QString TtsProviderConfiguration::validationError() const
{
    const QString endpointError = endpointValidationError(
        QStringLiteral("TTS"), provider, apiBaseUrl, model);
    if (!endpointError.isEmpty())
        return endpointError;
    if (voice.trimmed().isEmpty())
        return QStringLiteral("TTS voice 尚未配置");
    return {};
}

QString VoiceInteractionConfiguration::validationError() const
{
    if (requestTimeoutMs < 1'000 || requestTimeoutMs > 300'000)
        return QStringLiteral("AI 请求超时必须在 1000 到 300000 毫秒之间");
    if (vadThresholdDb < -96.0 || vadThresholdDb > 0.0)
        return QStringLiteral("VAD 音量阈值必须在 -96 到 0 dBFS 之间");
    if (vadSilenceTimeoutMs < 200 || vadSilenceTimeoutMs > 10'000)
        return QStringLiteral("VAD 静音等待必须在 200 到 10000 毫秒之间");
    if (recordingMinimumMs < 0 || recordingMinimumMs > 30'000)
        return QStringLiteral("最短录音时间必须在 0 到 30000 毫秒之间");
    if (vadMinimumSpeechMs < 40 || vadMinimumSpeechMs > 5'000)
        return QStringLiteral("VAD 最短语音必须在 40 到 5000 毫秒之间");
    if (recordingMaximumMs < 1'000 || recordingMaximumMs > 120'000)
        return QStringLiteral("最长录音时间必须在 1000 到 120000 毫秒之间");
    if (recordingMinimumMs >= recordingMaximumMs)
        return QStringLiteral("最短录音时间必须小于最长录音时间");
    if (sentenceMinimumCharacters < 1 || sentenceMinimumCharacters > 100)
        return QStringLiteral("句子最少字符数必须在 1 到 100 之间");
    if (sentenceMaximumCharacters < sentenceMinimumCharacters
        || sentenceMaximumCharacters > 500) {
        return QStringLiteral("句子最多字符数必须不小于最少字符数且不超过 500");
    }
    if (ttsPrebufferSegments < 1 || ttsPrebufferSegments > 4)
        return QStringLiteral("TTS 预缓冲段数必须在 1 到 4 之间");
    if (historyTurns < 0 || historyTurns > 20)
        return QStringLiteral("对话上下文轮数必须在 0 到 20 之间");
    return {};
}

QString AiConfiguration::validationError() const
{
    const QString asrError = asr.validationError();
    if (!asrError.isEmpty())
        return asrError;
    const QString llmError = llm.validationError();
    if (!llmError.isEmpty())
        return llmError;
    const QString ttsError = tts.validationError();
    if (!ttsError.isEmpty())
        return ttsError;
    return voice.validationError();
}

QString aiProviderErrorCodeName(AiProviderErrorCode code)
{
    switch (code) {
    case AiProviderErrorCode::NetworkError: return QStringLiteral("NetworkError");
    case AiProviderErrorCode::Timeout: return QStringLiteral("Timeout");
    case AiProviderErrorCode::Unauthorized: return QStringLiteral("Unauthorized");
    case AiProviderErrorCode::RateLimited: return QStringLiteral("RateLimited");
    case AiProviderErrorCode::ServerError: return QStringLiteral("ServerError");
    case AiProviderErrorCode::InvalidResponse: return QStringLiteral("InvalidResponse");
    case AiProviderErrorCode::EmptyResult: return QStringLiteral("EmptyResult");
    case AiProviderErrorCode::UnsupportedProvider: return QStringLiteral("UnsupportedProvider");
    case AiProviderErrorCode::Cancelled: return QStringLiteral("Cancelled");
    }
    return QStringLiteral("ServerError");
}
