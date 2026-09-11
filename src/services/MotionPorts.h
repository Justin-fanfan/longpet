#pragma once

#include "model/MotionModels.h"

#include <QHostAddress>
#include <QObject>

class MotionPort : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~MotionPort() override = default;

    virtual bool start(const QString& device, int baudRate,
                       QString* error = nullptr) = 0;
    virtual void stop() = 0;
    virtual bool isTransportAvailable() const = 0;
    virtual bool sendStop(QString* error = nullptr) = 0;
    virtual bool sendMode(MotionControlMode mode,
                          QString* error = nullptr) = 0;
    virtual bool sendMove(ChassisMotion motion, int speed,
                          QString* error = nullptr) = 0;
    virtual bool sendHead(HeadMotion motion, int stepUs,
                          QString* error = nullptr) = 0;
    virtual bool sendTarget(const MotionTargetFrame& target,
                            QString* error = nullptr) = 0;
    virtual bool requestStatus(QString* error = nullptr) = 0;

signals:
    void transportAvailabilityChanged(bool available, const QString& detail);
    void mcuActivity();
    void statusReceived(const MotionStatusSnapshot& status);
    void faultReported(const QString& reason);
};

class FamilyMotionControlPort : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~FamilyMotionControlPort() override = default;

    virtual bool start(QHostAddress address, quint16 port,
                       QString* error = nullptr) = 0;
    virtual void stop() = 0;
    virtual quint16 port() const = 0;
    virtual FamilyMotionSession createSession(int refreshIntervalMs,
                                               int leaseTimeoutMs,
                                               int defaultSpeed,
                                               int headStepUs,
                                               QString* error = nullptr) = 0;
    virtual void acceptController(const QString& sessionId) = 0;
    virtual void rejectController(const QString& sessionId,
                                  const QString& code,
                                  const QString& message) = 0;
    virtual void terminateController(const QString& code,
                                     const QString& message) = 0;
    virtual void publishStatus(const MotionStatusSnapshot& status) = 0;
    virtual bool hasController() const = 0;

signals:
    void controllerStartRequested(const QString& sessionId);
    void chassisCommandRequested(ChassisMotion motion, int speed);
    void stopRequested();
    void headCommandRequested(HeadMotion motion, int stepUs);
    void controllerStopped(const QString& reason);
};
