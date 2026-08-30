#include "AiConfigRepository.h"

#include <QFileInfo>
#include <QSettings>
#include <QDebug>

#include <utility>

namespace {
QString environmentOrValue(const char* environmentName, const QString& value)
{
    const QString environment = qEnvironmentVariable(environmentName).trimmed();
    return environment.isEmpty() ? value.trimmed() : environment;
}

QString environmentOrLegacy(const char* environmentName,
                            const char* legacyEnvironmentName,
                            const QString& value)
{
    const QString current = qEnvironmentVariable(environmentName).trimmed();
    if (!current.isEmpty())
        return current;
    return environmentOrValue(legacyEnvironmentName, value);
}

int boundedInteger(QSettings& settings, const QString& key, int fallback)
{
    bool valid = false;
    const int value = settings.value(key, fallback).toInt(&valid);
    return valid ? value : fallback;
}

int environmentInteger(const char* environmentName, int fallback)
{
    bool valid = false;
    const int value = qEnvironmentVariableIntValue(environmentName, &valid);
    return valid ? value : fallback;
}

bool hasGroup(const QSettings& settings, const QString& group)
{
    return settings.childGroups().contains(group, Qt::CaseInsensitive);
}
}

AiConfigRepository::AiConfigRepository(QString path)
    : m_path(std::move(path))
{
}

AiConfiguration AiConfigRepository::load(QString* error) const
{
    if (error)
        error->clear();
    AiConfiguration configuration;
    if (m_path.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("AI 配置文件路径为空");
        return configuration;
    }

    QSettings settings(m_path, QSettings::IniFormat);
    const bool legacyConfiguration = hasGroup(settings, QStringLiteral("ai"));
    const bool newConfiguration = hasGroup(settings, QStringLiteral("asr"))
        || hasGroup(settings, QStringLiteral("llm"))
        || hasGroup(settings, QStringLiteral("tts"))
        || hasGroup(settings, QStringLiteral("voice"));
    if (legacyConfiguration && newConfiguration) {
        qWarning().noquote()
            << "AI config contains legacy [ai] and split sections; split sections take precedence";
    }

    const QString legacyBaseUrl = settings.value(
        QStringLiteral("ai/api_base_url")).toString();
    const QString legacyApiKey = settings.value(
        QStringLiteral("ai/api_key")).toString();

    const bool hasAsr = hasGroup(settings, QStringLiteral("asr"));
    configuration.asr.provider = environmentOrValue(
        "LONGPET_ASR_PROVIDER", hasAsr
            ? settings.value(QStringLiteral("asr/provider")).toString()
            : QStringLiteral("openai-compatible"));
    configuration.asr.apiBaseUrl = QUrl(environmentOrLegacy(
        "LONGPET_ASR_BASE_URL", "LONGPET_AI_BASE_URL", hasAsr
            ? settings.value(QStringLiteral("asr/api_base_url")).toString()
            : legacyBaseUrl));
    configuration.asr.apiKey = environmentOrLegacy(
        "LONGPET_ASR_API_KEY", "LONGPET_AI_API_KEY", hasAsr
            ? settings.value(QStringLiteral("asr/api_key")).toString()
            : legacyApiKey);
    configuration.asr.model = environmentOrLegacy(
        "LONGPET_ASR_MODEL", "LONGPET_AI_ASR_MODEL", hasAsr
            ? settings.value(QStringLiteral("asr/model")).toString()
            : settings.value(QStringLiteral("ai/asr_model")).toString());
    configuration.asr.language = environmentOrValue(
        "LONGPET_ASR_LANGUAGE", hasAsr
            ? settings.value(QStringLiteral("asr/language"), QStringLiteral("zh")).toString()
            : settings.value(QStringLiteral("ai/language"), QStringLiteral("zh")).toString());

    const bool hasLlm = hasGroup(settings, QStringLiteral("llm"));
    configuration.llm.provider = environmentOrValue(
        "LONGPET_LLM_PROVIDER", hasLlm
            ? settings.value(QStringLiteral("llm/provider")).toString()
            : QStringLiteral("openai-compatible"));
    configuration.llm.apiBaseUrl = QUrl(environmentOrLegacy(
        "LONGPET_LLM_BASE_URL", "LONGPET_AI_BASE_URL", hasLlm
            ? settings.value(QStringLiteral("llm/api_base_url")).toString()
            : legacyBaseUrl));
    configuration.llm.apiKey = environmentOrLegacy(
        "LONGPET_LLM_API_KEY", "LONGPET_AI_API_KEY", hasLlm
            ? settings.value(QStringLiteral("llm/api_key")).toString()
            : legacyApiKey);
    configuration.llm.model = environmentOrLegacy(
        "LONGPET_LLM_MODEL", "LONGPET_AI_LLM_MODEL", hasLlm
            ? settings.value(QStringLiteral("llm/model")).toString()
            : settings.value(QStringLiteral("ai/llm_model")).toString());

    const bool hasTts = hasGroup(settings, QStringLiteral("tts"));
    configuration.tts.provider = environmentOrValue(
        "LONGPET_TTS_PROVIDER", hasTts
            ? settings.value(QStringLiteral("tts/provider")).toString()
            : QStringLiteral("openai-compatible"));
    configuration.tts.apiBaseUrl = QUrl(environmentOrLegacy(
        "LONGPET_TTS_BASE_URL", "LONGPET_AI_BASE_URL", hasTts
            ? settings.value(QStringLiteral("tts/api_base_url")).toString()
            : legacyBaseUrl));
    configuration.tts.apiKey = environmentOrLegacy(
        "LONGPET_TTS_API_KEY", "LONGPET_AI_API_KEY", hasTts
            ? settings.value(QStringLiteral("tts/api_key")).toString()
            : legacyApiKey);
    configuration.tts.model = environmentOrLegacy(
        "LONGPET_TTS_MODEL", "LONGPET_AI_TTS_MODEL", hasTts
            ? settings.value(QStringLiteral("tts/model")).toString()
            : settings.value(QStringLiteral("ai/tts_model")).toString());
    configuration.tts.voice = environmentOrLegacy(
        "LONGPET_TTS_VOICE", "LONGPET_AI_TTS_VOICE", hasTts
            ? settings.value(QStringLiteral("tts/voice")).toString()
            : settings.value(QStringLiteral("ai/tts_voice")).toString());

    const bool hasVoice = hasGroup(settings, QStringLiteral("voice"));
    const QString voicePrefix = hasVoice ? QStringLiteral("voice/") : QStringLiteral("ai/");
    configuration.voice.systemPrompt = environmentOrValue(
        "LONGPET_VOICE_SYSTEM_PROMPT",
        settings.value(voicePrefix + QStringLiteral("system_prompt")).toString());
    configuration.voice.requestTimeoutMs = environmentInteger(
        "LONGPET_VOICE_REQUEST_TIMEOUT_MS", boundedInteger(
            settings, voicePrefix + QStringLiteral("request_timeout_ms"), 30'000));
    configuration.voice.recordingMaximumMs = environmentInteger(
        "LONGPET_VOICE_RECORDING_MAXIMUM_MS", boundedInteger(
            settings, voicePrefix + QStringLiteral("recording_maximum_ms"), 12'000));
    configuration.voice.historyTurns = environmentInteger(
        "LONGPET_VOICE_HISTORY_TURNS", boundedInteger(
            settings, voicePrefix + QStringLiteral("history_turns"), 4));

    const QString validationError = configuration.validationError();
    if (error && !validationError.isEmpty()) {
        const QString prefix = QFileInfo::exists(m_path)
            ? QStringLiteral("AI 配置无效")
            : QStringLiteral("未找到 AI 配置文件 %1").arg(m_path);
        *error = QStringLiteral("%1：%2").arg(prefix, validationError);
    }
    return configuration;
}

QString AiConfigRepository::path() const
{
    return m_path;
}
