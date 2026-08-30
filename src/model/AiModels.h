#pragma once

#include <QList>
#include <QMetaType>
#include <QString>
#include <QUrl>

enum class VoiceInteractionState {
    Idle,
    Recording,
    Recognizing,
    Thinking,
    Speaking,
    Failed
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
    int recordingMaximumMs = 12'000;
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

    bool isActive() const
    {
        return state == VoiceInteractionState::Recording
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
