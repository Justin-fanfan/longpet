#pragma once

#include "model/AiModels.h"
#include "services/VoiceInteractionPorts.h"

#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>

class QNetworkReply;
class QNetworkRequest;

class OpenAiCompatibleProvider final : public AiProviderPort {
    Q_OBJECT

public:
    explicit OpenAiCompatibleProvider(const AiConfiguration& configuration,
                                      QObject* parent = nullptr);

    void transcribe(quint64 sessionId, const QByteArray& wavAudio) override;
    void completeChat(quint64 sessionId,
                      const QList<AiChatMessage>& messages) override;
    void synthesize(quint64 sessionId, const QString& text) override;
    void cancel(quint64 sessionId) override;

private:
    QUrl endpoint(const QString& relativePath) const;
    QNetworkRequest requestFor(const QString& relativePath) const;
    void watchReply(QNetworkReply* reply, quint64 sessionId, AiRequestStage stage);
    void handleFinished(QNetworkReply* reply);
    void emitFailure(quint64 sessionId, AiRequestStage stage,
                     const QString& message, const QString& diagnostic);
    QString stageName(AiRequestStage stage) const;

    AiConfiguration m_configuration;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QTimer m_timeout;
    quint64 m_sessionId = 0;
    AiRequestStage m_stage = AiRequestStage::Asr;
    bool m_timedOut = false;
};
