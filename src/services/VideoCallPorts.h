#pragma once

#include "model/VideoCallModels.h"

#include <QImage>
#include <QObject>

class VideoCallMediaPort : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~VideoCallMediaPort() override = default;

    virtual quint16 port() const = 0;
    virtual bool prepare(const VideoCallSnapshot& snapshot,
                         bool audioEnabled,
                         QString* error = nullptr) = 0;
    virtual void enableAudio() = 0;
    virtual void stop() = 0;

signals:
    void peerAuthenticated();
    void mediaReady();
    void failed(const QString& code, const QString& message);
    void disconnected(const QString& message);
    void remoteVideoFrame(const QImage& frame);
};

class CallPromptPlayerPort : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~CallPromptPlayerPort() override = default;

    virtual bool play(VideoCallMode mode, QString* error = nullptr) = 0;
    virtual void stop() = 0;

signals:
    void finished();
    void failed(const QString& message);
};
