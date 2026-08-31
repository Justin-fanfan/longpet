#pragma once

#include <QList>
#include <QMetaType>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUrl>

enum class VoiceInteractionState {
    Idle,
    Listening,
    Recognizing,
    Thinking,
    Speaking,
    Error,
    Offline,
    Cancelled
};

enum class VoiceAudioStage {
    Recording,
    Playback
};

enum class AiProviderErrorCode {
    NetworkError,
    Timeout,
    Unauthorized,
    RateLimited,
    ServerError,
    InvalidResponse,
    EmptyResult,
    UnsupportedProvider,
    Cancelled
};

struct AiProviderError {
    AiProviderErrorCode code = AiProviderErrorCode::ServerError;
    QString userMessage;
    QString diagnostic;
    QString provider;
    QString apiCode;
    int httpStatus = 0;
};

struct AiToolCall {
    QString id;
    QString name;
    QString argumentsJson;
};

struct AiChatMessage {
    QString role;
    QString content;
    QString name;
    QString toolCallId;
    QList<AiToolCall> toolCalls;
};

struct AiToolDefinition {
    QString name;
    QString description;
    QJsonObject parameters;
};

struct AiToolExecutionResult {
    bool success = false;
    QString content;
    QString userMessage;
};

struct KwsEvent {
    QString keyword;
    double score = 0.0;
    qint64 timestampMs = 0;
};

struct AsrProviderConfiguration {
    QString provider;
    QUrl apiBaseUrl;
    QString apiKey;
    QString model;
    QString language = QStringLiteral("zh");

    QString validationError() const;
};

struct LlmProviderConfiguration {
    QString provider;
    QUrl apiBaseUrl;
    QString apiKey;
    QString model;

    QString validationError() const;
};

struct TtsProviderConfiguration {
    QString provider;
    QUrl apiBaseUrl;
    QString apiKey;
    QString model;
    QString voice;

    QString validationError() const;
};

struct VoiceInteractionConfiguration {
    QString systemPrompt;
    int requestTimeoutMs = 30'000;
    bool vadEnabled = true;
    double vadThresholdDb = -42.0;
    int vadSilenceTimeoutMs = 900;
    int recordingMinimumMs = 600;
    int vadMinimumSpeechMs = 160;
    int recordingMaximumMs = 12'000;
    bool llmStreamEnabled = true;
    bool sentenceTtsEnabled = true;
    int sentenceMinimumCharacters = 6;
    int sentenceMaximumCharacters = 120;
    int ttsPrebufferSegments = 2;
    int historyTurns = 4;
    int availabilityRetryMs = 30'000;

    QString validationError() const;
};

struct KwsConfiguration {
    bool enabled = false;
    QString pythonProgram = QStringLiteral("python3");
    QString bridgeScript = QStringLiteral("/home/longpet/longpet-kws/longpet_kws_bridge.py");
    QString kwsRoot = QStringLiteral("/home/longpet/longpet-kws/upstream");
    QString modelPath = QStringLiteral("/home/longpet/longpet-kws/upstream/assets/fsmn/fsmn_ctc.onnx");
    QString tokensPath = QStringLiteral("/home/longpet/longpet-kws/upstream/assets/fsmn/tokens.txt");
    QString captureBackend = QStringLiteral("sounddevice");
    QString inputDevice;
    QString alsaDevice;
    int inputSampleRate = 48'000;
    double wakeThreshold = 0.15;
    double ignoredHelloThreshold = 0.10;
    double companionThreshold = 0.05;
    double emergencyThreshold = 0.05;
    double vadThresholdDb = -60.0;
    double vadNoiseRatio = 2.5;
    int commandTimeoutMs = 10'000;
    int pauseTimeoutMs = 1'500;
    int resumeCooldownMs = 1'200;
    int restartDelayMs = 2'000;

    QString validationError() const;
};

struct OfflineVoiceConfiguration {
    bool enabled = true;
    QString companionAudioDirectory = QStringLiteral("/home/longpet/offline-audio");

    QString validationError() const;
};

struct VoiceToolConfiguration {
    bool enabled = true;
    int maximumRounds = 3;

    QString validationError() const;
};

struct AiConfiguration {
    AsrProviderConfiguration asr;
    LlmProviderConfiguration llm;
    TtsProviderConfiguration tts;
    VoiceInteractionConfiguration voice;
    KwsConfiguration kws;
    OfflineVoiceConfiguration offline;
    VoiceToolConfiguration tools;

    QString validationError() const;
    bool isValid() const { return validationError().isEmpty(); }
};

struct VoiceInteractionSnapshot {
    quint64 sessionId = 0;
    VoiceInteractionState state = VoiceInteractionState::Idle;
    QString transcript;
    QString response;
    QString statusMessage;
    QString errorMessage;
    bool generationActive = false;
    bool playbackActive = false;
    bool speechDetected = false;

    bool isActive() const
    {
        return state == VoiceInteractionState::Listening
            || state == VoiceInteractionState::Recognizing
            || state == VoiceInteractionState::Thinking
            || state == VoiceInteractionState::Speaking;
    }
};

struct VoiceInteractionResult {
    bool success = false;
    QString error;
    VoiceInteractionSnapshot snapshot;
};

QString aiProviderErrorCodeName(AiProviderErrorCode code);

Q_DECLARE_METATYPE(VoiceInteractionState)
Q_DECLARE_METATYPE(VoiceAudioStage)
Q_DECLARE_METATYPE(AiProviderErrorCode)
Q_DECLARE_METATYPE(AiProviderError)
Q_DECLARE_METATYPE(VoiceInteractionSnapshot)
Q_DECLARE_METATYPE(AiToolCall)
Q_DECLARE_METATYPE(QList<AiToolCall>)
Q_DECLARE_METATYPE(KwsEvent)
