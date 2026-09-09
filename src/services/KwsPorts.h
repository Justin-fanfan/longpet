#pragma once

#include "model/AiModels.h"

#include <QObject>

class KwsPort : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~KwsPort() override = default;

    virtual void start() = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void stop() = 0;
    // Running includes model loading. Paused means capture has positively released
    // the microphone, not just that a pause command was sent.
    virtual bool isRunning() const = 0;
    virtual bool isPaused() const = 0;

signals:
    void kwsReady();
    void keywordDetected(const KwsEvent& event);
    void kwsError(const QString& userMessage, const QString& diagnostic);
    void kwsPaused();
    void kwsResumed();
    void kwsStopped();
};
