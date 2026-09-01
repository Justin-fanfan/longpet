#pragma once

#include "model/CameraModels.h"

#include <QObject>
#include <QString>

class CameraSourcePort : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~CameraSourcePort() override = default;

    virtual bool acquire(QObject* consumer, QString* error = nullptr) = 0;
    virtual void release(QObject* consumer) = 0;
    virtual bool isAvailable() const = 0;
    virtual int consumerCount() const = 0;
    virtual CameraFrame latestFrame() const = 0;

signals:
    void frameReady(const CameraFrame& frame);
    void availabilityChanged(bool available, const QString& message);
    void failed(const QString& code, const QString& message);
};
