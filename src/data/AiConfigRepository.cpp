#include "AiConfigRepository.h"

#include <QFileInfo>
#include <QSettings>

#include <utility>

namespace {
QString environmentOrValue(const char* environmentName, const QString& value)
{
    const QString environment = qEnvironmentVariable(environmentName).trimmed();
    return environment.isEmpty() ? value.trimmed() : environment;
}

int boundedInteger(QSettings& settings, const QString& key, int fallback)
{
    bool valid = false;
    const int value = settings.value(key, fallback).toInt(&valid);
    return valid ? value : fallback;
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
    const QString baseUrl = environmentOrValue(
        "LONGPET_AI_BASE_URL", settings.value(QStringLiteral("ai/api_base_url")).toString());
    configuration.apiBaseUrl = QUrl(baseUrl);
    configuration.apiKey = environmentOrValue(
        "LONGPET_AI_API_KEY", settings.value(QStringLiteral("ai/api_key")).toString());
    configuration.asrModel = environmentOrValue(
        "LONGPET_AI_ASR_MODEL", settings.value(QStringLiteral("ai/asr_model")).toString());
    configuration.llmModel = environmentOrValue(
        "LONGPET_AI_LLM_MODEL", settings.value(QStringLiteral("ai/llm_model")).toString());
    configuration.ttsModel = environmentOrValue(
        "LONGPET_AI_TTS_MODEL", settings.value(QStringLiteral("ai/tts_model")).toString());
    configuration.ttsVoice = environmentOrValue(
        "LONGPET_AI_TTS_VOICE", settings.value(QStringLiteral("ai/tts_voice")).toString());
    configuration.systemPrompt = settings.value(
        QStringLiteral("ai/system_prompt")).toString().trimmed();
    configuration.language = settings.value(
        QStringLiteral("ai/language"), QStringLiteral("zh")).toString().trimmed();
    configuration.requestTimeoutMs = boundedInteger(
        settings, QStringLiteral("ai/request_timeout_ms"), 30'000);
    configuration.recordingMaximumMs = boundedInteger(
        settings, QStringLiteral("ai/recording_maximum_ms"), 12'000);
    configuration.historyTurns = boundedInteger(
        settings, QStringLiteral("ai/history_turns"), 4);

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
