#pragma once

#include "model/CameraModels.h"
#include "model/FamilyVisionModels.h"

#include <QHostAddress>
#include <QObject>

class FamilyVisionStreamPort : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~FamilyVisionStreamPort() override = default;

    virtual bool start(QHostAddress address, quint16 port,
                       QString* error = nullptr) = 0;
    virtual void stop() = 0;
    virtual quint16 port() const = 0;
    virtual FamilyVisionSession createSession(int frameRate,
                                               QString* error = nullptr) = 0;
    virtual void acceptViewer(const QString& sessionId) = 0;
    virtual void rejectViewer(const QString& sessionId,
                              const QString& code,
                              const QString& message) = 0;
    virtual void publishCameraFrame(const CameraFrame& frame) = 0;
    virtual void publishTelemetry(const FamilyVisionTelemetry& telemetry) = 0;
    virtual bool hasViewer() const = 0;

signals:
    void viewerStartRequested(const QString& sessionId);
    void viewerStopped(const QString& reason);
};
