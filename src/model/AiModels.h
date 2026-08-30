#pragma once

#include <QList>
#include <QMetaType>
#include <QString>
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

struct AiChatMessage {
    QString role;
    QString content;
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

    QString validationError() const;
};

struct AiConfiguration {
    AsrProviderConfiguration asr;
    LlmProviderConfiguration llm;
    TtsProviderConfiguration tts;
    VoiceInteractionConfiguration voice;

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
