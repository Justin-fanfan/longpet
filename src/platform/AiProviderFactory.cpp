#include "AiProviderFactory.h"

#include "platform/AliyunProviders.h"
#include "platform/OpenAiCompatibleProviders.h"
#include "services/VoiceInteractionPorts.h"

#include <QDebug>

namespace {
QString normalizedProvider(const QString& provider)
{
    return provider.trimmed().toLower();
}

AiProviderError unsupportedError(const QString& capability,
                                 const QString& provider)
{
    return providerResponseError(
        AiProviderErrorCode::UnsupportedProvider,
        provider.isEmpty() ? QStringLiteral("unconfigured") : provider,
        QStringLiteral("当前 %1 Provider 不受支持，请检查配置").arg(capability),
        QStringLiteral("unsupported capability=%1 provider=%2")
            .arg(capability, provider));
}

class UnsupportedAsrProvider final : public AsrProviderPort {
public:
    explicit UnsupportedAsrProvider(QString provider)
        : m_provider(std::move(provider)) {}
    void transcribe(quint64 sessionId, const QByteArray&) override
    { emit requestFailed(sessionId, unsupportedError(QStringLiteral("ASR"), m_provider)); }
    void cancel(quint64) override {}
private:
    QString m_provider;
};

class UnsupportedLlmProvider final : public LlmProviderPort {
public:
    explicit UnsupportedLlmProvider(QString provider)
        : m_provider(std::move(provider)) {}
    void completeChat(quint64 sessionId, const QList<AiChatMessage>&) override
    { emit requestFailed(sessionId, unsupportedError(QStringLiteral("LLM"), m_provider)); }
    void cancel(quint64) override {}
private:
    QString m_provider;
};

class UnsupportedTtsProvider final : public TtsProviderPort {
public:
    explicit UnsupportedTtsProvider(QString provider)
        : m_provider(std::move(provider)) {}
    void synthesize(quint64 sessionId, const QString&) override
    { emit requestFailed(sessionId, unsupportedError(QStringLiteral("TTS"), m_provider)); }
    void cancel(quint64) override {}
private:
    QString m_provider;
};
}

std::unique_ptr<AsrProviderPort> AiProviderFactory::createAsr(
    const AsrProviderConfiguration& configuration, int timeoutMs)
{
    const QString provider = normalizedProvider(configuration.provider);
    qInfo().noquote() << "AI ASR provider:" << provider;
    if (provider == QStringLiteral("aliyun")
        || provider == QStringLiteral("dashscope")) {
        return std::make_unique<AliyunAsrProvider>(configuration, timeoutMs);
    }
    if (provider == QStringLiteral("openai")
        || provider == QStringLiteral("openai-compatible")) {
        return std::make_unique<OpenAiAsrProvider>(configuration, timeoutMs);
    }
    return std::make_unique<UnsupportedAsrProvider>(provider);
}

std::unique_ptr<LlmProviderPort> AiProviderFactory::createLlm(
    const LlmProviderConfiguration& configuration, int timeoutMs)
{
    const QString provider = normalizedProvider(configuration.provider);
    qInfo().noquote() << "AI LLM provider:" << provider;
    if (provider == QStringLiteral("openai")
        || provider == QStringLiteral("openai-compatible")
        || provider == QStringLiteral("aliyun")
        || provider == QStringLiteral("dashscope")) {
        return std::make_unique<OpenAiCompatibleLlmProvider>(configuration, timeoutMs);
    }
    return std::make_unique<UnsupportedLlmProvider>(provider);
}

std::unique_ptr<TtsProviderPort> AiProviderFactory::createTts(
    const TtsProviderConfiguration& configuration, int timeoutMs)
{
    const QString provider = normalizedProvider(configuration.provider);
    qInfo().noquote() << "AI TTS provider:" << provider;
    if (provider == QStringLiteral("aliyun")
        || provider == QStringLiteral("dashscope")) {
        return std::make_unique<AliyunTtsProvider>(configuration, timeoutMs);
    }
    if (provider == QStringLiteral("openai")
        || provider == QStringLiteral("openai-compatible")) {
        return std::make_unique<OpenAiTtsProvider>(configuration, timeoutMs);
    }
    return std::make_unique<UnsupportedTtsProvider>(provider);
}
