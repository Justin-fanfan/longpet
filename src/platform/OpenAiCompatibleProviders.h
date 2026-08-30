#pragma once

#include "model/AiModels.h"
#include "platform/ProviderHttpClient.h"
#include "platform/SseEventParser.h"
#include "services/VoiceInteractionPorts.h"

class OpenAiAsrProvider final : public AsrProviderPort {
    Q_OBJECT

public:
    OpenAiAsrProvider(const AsrProviderConfiguration& configuration,
                      int timeoutMs, QObject* parent = nullptr);

    void transcribe(quint64 sessionId, const QByteArray& wavAudio) override;
    void cancel(quint64 sessionId) override;

private:
    void handleResponse(quint64 sessionId, const ProviderHttpResponse& response);

    AsrProviderConfiguration m_configuration;
    ProviderHttpClient m_http;
};

class OpenAiCompatibleLlmProvider final : public LlmProviderPort {
    Q_OBJECT

public:
    OpenAiCompatibleLlmProvider(const LlmProviderConfiguration& configuration,
                                int timeoutMs, QObject* parent = nullptr);

    void completeChat(quint64 sessionId,
                      const QList<AiChatMessage>& messages) override;
    void streamChat(quint64 sessionId,
                    const QList<AiChatMessage>& messages) override;
    void cancel(quint64 sessionId) override;

private:
    void handleResponse(quint64 sessionId, const ProviderHttpResponse& response);
    void handleStreamChunk(quint64 sessionId, const QByteArray& chunk);
    bool processStreamEvents(quint64 sessionId,
                             const QList<QByteArray>& events);
    void resetStream();

    LlmProviderConfiguration m_configuration;
    ProviderHttpClient m_http;
    SseEventParser m_sseParser;
    quint64 m_streamSessionId = 0;
    QString m_streamText;
    bool m_streamDone = false;
    bool m_streamFinishReasonSeen = false;
};

class OpenAiTtsProvider final : public TtsProviderPort {
    Q_OBJECT

public:
    OpenAiTtsProvider(const TtsProviderConfiguration& configuration,
                      int timeoutMs, QObject* parent = nullptr);

    void synthesize(quint64 sessionId, const QString& text) override;
    void cancel(quint64 sessionId) override;

private:
    void handleResponse(quint64 sessionId, const ProviderHttpResponse& response);

    TtsProviderConfiguration m_configuration;
    ProviderHttpClient m_http;
};
