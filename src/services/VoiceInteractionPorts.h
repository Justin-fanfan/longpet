#pragma once

#include "model/AiModels.h"

#include <QByteArray>
#include <QObject>

class AsrProviderPort : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~AsrProviderPort() override = default;

    virtual void transcribe(quint64 sessionId, const QByteArray& wavAudio) = 0;
    virtual void cancel(quint64 sessionId) = 0;

signals:
    void transcriptionReady(quint64 sessionId, const QString& text);
    void requestFailed(quint64 sessionId, const AiProviderError& error);
};

class LlmProviderPort : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~LlmProviderPort() override = default;

    virtual void completeChat(quint64 sessionId,
                              const QList<AiChatMessage>& messages) = 0;
    virtual void cancel(quint64 sessionId) = 0;

signals:
    void chatCompletionReady(quint64 sessionId, const QString& text);
    void requestFailed(quint64 sessionId, const AiProviderError& error);
};

class TtsProviderPort : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~TtsProviderPort() override = default;

    virtual void synthesize(quint64 sessionId, const QString& text) = 0;
    virtual void cancel(quint64 sessionId) = 0;

signals:
    void speechReady(quint64 sessionId, const QByteArray& audio);
    void requestFailed(quint64 sessionId, const AiProviderError& error);
};

class VoiceAudioPort : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~VoiceAudioPort() override = default;

    virtual void startRecording(quint64 sessionId) = 0;
    virtual void finishRecording(quint64 sessionId) = 0;
    virtual void play(quint64 sessionId, const QByteArray& audio) = 0;
    virtual void cancel(quint64 sessionId) = 0;

signals:
    void recordingStarted(quint64 sessionId);
    void recordingReady(quint64 sessionId, const QByteArray& wavAudio);
    void playbackStarted(quint64 sessionId);
    void playbackFinished(quint64 sessionId);
    void audioFailed(quint64 sessionId, VoiceAudioStage stage,
                     const QString& userMessage, const QString& diagnostic);
};
