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

double boundedDouble(QSettings& settings, const QString& key, double fallback)
{
    bool valid = false;
    const double value = settings.value(key, fallback).toDouble(&valid);
    return valid ? value : fallback;
}

double environmentDouble(const char* environmentName, double fallback)
{
    bool valid = false;
    const double value = qEnvironmentVariable(environmentName).toDouble(&valid);
    return valid ? value : fallback;
}

bool environmentBoolean(const char* environmentName, bool fallback)
{
    const QString value = qEnvironmentVariable(environmentName).trimmed().toLower();
    if (value.isEmpty())
        return fallback;
    if (value == QStringLiteral("1") || value == QStringLiteral("true")
        || value == QStringLiteral("yes") || value == QStringLiteral("on")) {
        return true;
    }
    if (value == QStringLiteral("0") || value == QStringLiteral("false")
        || value == QStringLiteral("no") || value == QStringLiteral("off")) {
        return false;
    }
    return fallback;
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
    configuration.voice.vadEnabled = environmentBoolean(
        "LONGPET_VOICE_VAD_ENABLED", settings.value(
            voicePrefix + QStringLiteral("vad_enabled"), true).toBool());
    configuration.voice.vadThresholdDb = environmentDouble(
        "LONGPET_VOICE_VAD_THRESHOLD_DB", boundedDouble(
            settings, voicePrefix + QStringLiteral("vad_threshold_db"), -42.0));
    configuration.voice.vadSilenceTimeoutMs = environmentInteger(
        "LONGPET_VOICE_VAD_SILENCE_TIMEOUT_MS", boundedInteger(
            settings, voicePrefix + QStringLiteral("vad_silence_timeout_ms"), 900));
    configuration.voice.recordingMinimumMs = environmentInteger(
        "LONGPET_VOICE_RECORDING_MINIMUM_MS", boundedInteger(
            settings, voicePrefix + QStringLiteral("recording_minimum_ms"), 600));
    configuration.voice.vadMinimumSpeechMs = environmentInteger(
        "LONGPET_VOICE_VAD_MINIMUM_SPEECH_MS", boundedInteger(
            settings, voicePrefix + QStringLiteral("vad_minimum_speech_ms"), 160));
    configuration.voice.recordingMaximumMs = environmentInteger(
        "LONGPET_VOICE_RECORDING_MAXIMUM_MS", boundedInteger(
            settings, voicePrefix + QStringLiteral("recording_maximum_ms"), 12'000));
    configuration.voice.llmStreamEnabled = environmentBoolean(
        "LONGPET_VOICE_LLM_STREAM_ENABLED", settings.value(
            voicePrefix + QStringLiteral("llm_stream_enabled"), true).toBool());
    configuration.voice.sentenceTtsEnabled = environmentBoolean(
        "LONGPET_VOICE_SENTENCE_TTS_ENABLED", settings.value(
            voicePrefix + QStringLiteral("sentence_tts_enabled"), true).toBool());
    configuration.voice.sentenceMinimumCharacters = environmentInteger(
        "LONGPET_VOICE_SENTENCE_MINIMUM_CHARACTERS", boundedInteger(
            settings, voicePrefix + QStringLiteral("sentence_minimum_characters"), 6));
    configuration.voice.sentenceMaximumCharacters = environmentInteger(
        "LONGPET_VOICE_SENTENCE_MAXIMUM_CHARACTERS", boundedInteger(
            settings, voicePrefix + QStringLiteral("sentence_maximum_characters"), 120));
    configuration.voice.ttsPrebufferSegments = environmentInteger(
        "LONGPET_VOICE_TTS_PREBUFFER_SEGMENTS", boundedInteger(
            settings, voicePrefix + QStringLiteral("tts_prebuffer_segments"), 2));
    configuration.voice.historyTurns = environmentInteger(
        "LONGPET_VOICE_HISTORY_TURNS", boundedInteger(
            settings, voicePrefix + QStringLiteral("history_turns"), 4));
    configuration.voice.availabilityRetryMs = environmentInteger(
        "LONGPET_VOICE_AVAILABILITY_RETRY_MS", boundedInteger(
            settings, voicePrefix + QStringLiteral("availability_retry_ms"), 30'000));

    configuration.kws.enabled = environmentBoolean(
        "LONGPET_KWS_ENABLED", settings.value(
            QStringLiteral("kws/enabled"), false).toBool());
    configuration.kws.pythonProgram = environmentOrValue(
        "LONGPET_KWS_PYTHON", settings.value(
            QStringLiteral("kws/python_program"), QStringLiteral("python3")).toString());
    configuration.kws.bridgeScript = environmentOrValue(
        "LONGPET_KWS_BRIDGE_SCRIPT", settings.value(
            QStringLiteral("kws/bridge_script"),
            QStringLiteral("/home/longpet/longpet-kws/longpet_kws_bridge.py")).toString());
    configuration.kws.kwsRoot = environmentOrValue(
        "LONGPET_KWS_ROOT", settings.value(
            QStringLiteral("kws/kws_root"),
            QStringLiteral("/home/longpet/longpet-kws/upstream")).toString());
    configuration.kws.modelPath = environmentOrValue(
        "LONGPET_KWS_MODEL", settings.value(
            QStringLiteral("kws/model_path"),
            QStringLiteral("/home/longpet/longpet-kws/upstream/assets/fsmn/fsmn_ctc.onnx")).toString());
    configuration.kws.tokensPath = environmentOrValue(
        "LONGPET_KWS_TOKENS", settings.value(
            QStringLiteral("kws/tokens_path"),
            QStringLiteral("/home/longpet/longpet-kws/upstream/assets/fsmn/tokens.txt")).toString());
    configuration.kws.captureBackend = environmentOrValue(
        "LONGPET_KWS_CAPTURE_BACKEND", settings.value(
            QStringLiteral("kws/capture_backend"),
            QStringLiteral("sounddevice")).toString()).toLower();
    configuration.kws.inputDevice = environmentOrValue(
        "LONGPET_KWS_INPUT_DEVICE", settings.value(
            QStringLiteral("kws/input_device")).toString());
    configuration.kws.alsaDevice = environmentOrValue(
        "LONGPET_KWS_ALSA_DEVICE",
        environmentOrValue(
            "LONGPET_AI_CAPTURE_DEVICE", settings.value(
                QStringLiteral("kws/alsa_device")).toString()));
    configuration.kws.inputSampleRate = environmentInteger(
        "LONGPET_KWS_INPUT_SAMPLE_RATE", boundedInteger(
            settings, QStringLiteral("kws/input_sample_rate"), 48'000));
    configuration.kws.wakeThreshold = environmentDouble(
        "LONGPET_KWS_WAKE_THRESHOLD", boundedDouble(
            settings, QStringLiteral("kws/wake_threshold"), 0.15));
    configuration.kws.ignoredHelloThreshold = environmentDouble(
        "LONGPET_KWS_HELLO_THRESHOLD", boundedDouble(
            settings, QStringLiteral("kws/hello_threshold"), 0.10));
    configuration.kws.companionThreshold = environmentDouble(
        "LONGPET_KWS_COMPANION_THRESHOLD", boundedDouble(
            settings, QStringLiteral("kws/companion_threshold"), 0.05));
    configuration.kws.emergencyThreshold = environmentDouble(
        "LONGPET_KWS_EMERGENCY_THRESHOLD", boundedDouble(
            settings, QStringLiteral("kws/emergency_threshold"), 0.05));
    configuration.kws.vadThresholdDb = environmentDouble(
        "LONGPET_KWS_VAD_THRESHOLD_DB", boundedDouble(
            settings, QStringLiteral("kws/vad_threshold_db"), -60.0));
    configuration.kws.vadNoiseRatio = environmentDouble(
        "LONGPET_KWS_VAD_NOISE_RATIO", boundedDouble(
            settings, QStringLiteral("kws/vad_noise_ratio"), 2.5));
    configuration.kws.commandTimeoutMs = environmentInteger(
        "LONGPET_KWS_COMMAND_TIMEOUT_MS", boundedInteger(
            settings, QStringLiteral("kws/command_timeout_ms"), 10'000));
    configuration.kws.pauseTimeoutMs = environmentInteger(
        "LONGPET_KWS_PAUSE_TIMEOUT_MS", boundedInteger(
            settings, QStringLiteral("kws/pause_timeout_ms"), 1'500));
    configuration.kws.resumeCooldownMs = environmentInteger(
        "LONGPET_KWS_RESUME_COOLDOWN_MS", boundedInteger(
            settings, QStringLiteral("kws/resume_cooldown_ms"), 1'200));
    configuration.kws.restartDelayMs = environmentInteger(
        "LONGPET_KWS_RESTART_DELAY_MS", boundedInteger(
            settings, QStringLiteral("kws/restart_delay_ms"), 2'000));

    configuration.offline.enabled = environmentBoolean(
        "LONGPET_OFFLINE_VOICE_ENABLED", settings.value(
            QStringLiteral("offline/enabled"), true).toBool());
    configuration.offline.companionAudioDirectory = environmentOrValue(
        "LONGPET_OFFLINE_COMPANION_AUDIO_DIR", settings.value(
            QStringLiteral("offline/companion_audio_directory"),
            QStringLiteral("/home/longpet/offline-audio")).toString());

    configuration.tools.enabled = environmentBoolean(
        "LONGPET_VOICE_TOOLS_ENABLED", settings.value(
            QStringLiteral("tools/enabled"), true).toBool());
    configuration.tools.maximumRounds = environmentInteger(
        "LONGPET_VOICE_TOOLS_MAXIMUM_ROUNDS", boundedInteger(
            settings, QStringLiteral("tools/maximum_rounds"), 3));

    const QString validationError = configuration.validationError();
    if (error && !validationError.isEmpty()) {
        const QString prefix = QFileInfo::exists(m_path)
            ? QStringLiteral("AI 配置无效")
            : QStringLiteral("未找到 AI 配置文件 %1").arg(m_path);
        *error = QStringLiteral("%1：%2").arg(prefix, validationError);
    }
    const QString kwsError = configuration.kws.validationError();
    if (!kwsError.isEmpty())
        qWarning().noquote() << "KWS config invalid:" << kwsError;
    const QString offlineError = configuration.offline.validationError();
    if (!offlineError.isEmpty())
        qWarning().noquote() << "Offline voice config invalid:" << offlineError;
    const QString toolsError = configuration.tools.validationError();
    if (!toolsError.isEmpty())
        qWarning().noquote() << "Voice tools config invalid:" << toolsError;
    return configuration;
}

QString AiConfigRepository::path() const
{
    return m_path;
}
