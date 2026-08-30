#pragma once

#include "model/AiModels.h"
#include "platform/ProviderHttpClient.h"
#include "services/VoiceInteractionPorts.h"

class AliyunAsrProvider final : public AsrProviderPort {
    Q_OBJECT

public:
    AliyunAsrProvider(const AsrProviderConfiguration& configuration,
                      int timeoutMs, QObject* parent = nullptr);

    void transcribe(quint64 sessionId, const QByteArray& wavAudio) override;
    void cancel(quint64 sessionId) override;

private:
    void handleResponse(quint64 sessionId, const ProviderHttpResponse& response);
    bool usesCurrentAudioSchema() const;

    AsrProviderConfiguration m_configuration;
    ProviderHttpClient m_http;
};

class AliyunTtsProvider final : public TtsProviderPort {
    Q_OBJECT

public:
    AliyunTtsProvider(const TtsProviderConfiguration& configuration,
                      int timeoutMs, QObject* parent = nullptr);

    void synthesize(quint64 sessionId, const QString& text) override;
    void cancel(quint64 sessionId) override;

private:
    enum class Phase { Generate, Download };

    void handleResponse(quint64 sessionId, const ProviderHttpResponse& response);
    bool usesSpeechSynthesizerEndpoint() const;

    TtsProviderConfiguration m_configuration;
    ProviderHttpClient m_http;
    Phase m_phase = Phase::Generate;
};
