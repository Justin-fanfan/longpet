#pragma once

#include "model/FamilyVisionModels.h"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QObject>

class CameraFrame;
class CameraSourcePort;
class FamilyVisionStreamPort;
class VisionService;

class FamilyVisionMonitorService final : public QObject {
    Q_OBJECT

public:
    FamilyVisionMonitorService(CameraSourcePort* cameraSource,
                               VisionService* visionService,
                               FamilyVisionStreamPort* streamPort,
                               int frameRate = -1,
                               QObject* parent = nullptr);
    ~FamilyVisionMonitorService() override;

    bool start(QHostAddress address, quint16 port, QString* error = nullptr);
    void stop();
    FamilyVisionSession createSession(QString* error = nullptr);
    bool isAvailable() const;
    bool hasViewer() const;
    int frameRate() const;

private:
    void handleViewerStartRequested(const QString& sessionId);
    void handleViewerStopped(const QString& reason);
    void handleCameraFrame(const CameraFrame& frame);
    void handleTargetObservation(const TargetObservation& observation);
    void releaseCamera();
    static int configuredFrameRate();

    CameraSourcePort* m_cameraSource = nullptr;
    VisionService* m_visionService = nullptr;
    FamilyVisionStreamPort* m_streamPort = nullptr;
    FamilyVisionTelemetry m_latestTelemetry;
    QElapsedTimer m_frameClock;
    QDateTime m_previousObservationTime;
    double m_targetUpdateHz = 0.0;
    int m_frameRate = 7;
    bool m_started = false;
    bool m_cameraAcquired = false;
};
