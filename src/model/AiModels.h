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

enum class AiRequestStage {
    Asr,
    Llm,
    Tts
};

enum class VoiceAudioStage {
    Recording,
    Playback
};

struct AiChatMessage {
    QString role;
    QString content;
};

struct AiConfiguration {
    QUrl apiBaseUrl;
    QString apiKey;
    QString asrModel;
    QString llmModel;
    QString ttsModel;
    QString ttsVoice;
    QString systemPrompt;
    QString language = QStringLiteral("zh");
    int requestTimeoutMs = 30'000;
    int recordingMaximumMs = 12'000;
    int historyTurns = 4;

    QString validationError() const
    {
        if (!apiBaseUrl.isValid()
            || (apiBaseUrl.scheme() != QStringLiteral("http")
                && apiBaseUrl.scheme() != QStringLiteral("https"))) {
            return QStringLiteral("AI Gateway 地址未配置或格式不正确");
        }
        if (asrModel.trimmed().isEmpty())
            return QStringLiteral("ASR 模型尚未配置");
        if (llmModel.trimmed().isEmpty())
            return QStringLiteral("LLM 模型尚未配置");
        if (ttsModel.trimmed().isEmpty())
            return QStringLiteral("TTS 模型尚未配置");
        if (ttsVoice.trimmed().isEmpty())
            return QStringLiteral("TTS voice 尚未配置");
        if (requestTimeoutMs < 1'000 || requestTimeoutMs > 300'000)
            return QStringLiteral("AI 请求超时必须在 1000 到 300000 毫秒之间");
        if (recordingMaximumMs < 1'000 || recordingMaximumMs > 120'000)
            return QStringLiteral("最长录音时间必须在 1000 到 120000 毫秒之间");
        if (historyTurns < 0 || historyTurns > 20)
            return QStringLiteral("对话上下文轮数必须在 0 到 20 之间");
        return {};
    }

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

Q_DECLARE_METATYPE(VoiceInteractionState)
Q_DECLARE_METATYPE(AiRequestStage)
Q_DECLARE_METATYPE(VoiceAudioStage)
Q_DECLARE_METATYPE(VoiceInteractionSnapshot)
