#pragma once

#include "services/CameraPorts.h"

#include <QByteArray>
#include <QHash>
#include <QProcess>
#include <QSet>

class CameraCaptureAdapter : public CameraSourcePort {
    Q_OBJECT

public:
    explicit CameraCaptureAdapter(QObject* parent = nullptr);
    ~CameraCaptureAdapter() override;

    bool acquire(QObject* consumer, QString* error = nullptr) override;
    void release(QObject* consumer) override;
    bool isAvailable() const override;
    int consumerCount() const override;
    CameraFrame latestFrame() const override;

    static QString configuredDevice();

protected:
    virtual bool startCapture(QString* error);
    virtual void stopCapture();
    void ingestCameraBytes(const QByteArray& bytes);

private:
    void releaseConsumer(QObject* consumer, bool disconnectDestroyedSignal);
    void processCameraOutput();
    void handleProcessFinished(int exitCode, QProcess::ExitStatus status);
    void publishFrame(QByteArray jpeg);

    QProcess m_cameraProcess;
    QSet<QObject*> m_consumers;
    QHash<QObject*, QMetaObject::Connection> m_destroyConnections;
    QByteArray m_cameraBuffer;
    CameraFrame m_latestFrame;
    quint64 m_nextSequence = 0;
    bool m_running = false;
    bool m_stoppingProcess = false;
};
