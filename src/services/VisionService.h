#pragma once

#include "model/VisionModels.h"

#include <QObject>

#include <memory>

class CameraFrame;
class CameraSourcePort;
class VisionDetectorPort;
class VisionTrackerPort;
class VisionInferenceThread;

struct VisionTrackingConfiguration {
    bool trackingEnabled = true;
    int trackerIntervalMs = 100;
    int detectorCorrectionIntervalMs = 8'000;
    int searchDetectorIntervalMs = 250;
    int lostDetectorIntervalMs = 250;
    int freshnessTimeoutMs = 2'500;
    double minimumTrackerConfidence = 0.15;
    int lowConfidenceFrameLimit = 3;

    static VisionTrackingConfiguration fromEnvironment();
};

class VisionService final : public QObject {
    Q_OBJECT

public:
    explicit VisionService(CameraSourcePort* cameraSource,
                           VisionDetectorPort* detector,
                           int minimumIntervalMs = -1,
                           QObject* parent = nullptr);
    VisionService(CameraSourcePort* cameraSource,
                  VisionDetectorPort* detector,
                  VisionTrackerPort* tracker,
                  VisionTrackingConfiguration configuration =
                      VisionTrackingConfiguration::fromEnvironment(),
                  QObject* parent = nullptr);
    ~VisionService() override;

    void start();
    void stop();
    bool isRunning() const;
    bool isAvailable() const;
    bool isPaused() const;
    int minimumIntervalMs() const;

public slots:
    void setPaused(bool paused);
    void setVideoCallActive(bool active);

signals:
    void visionResultReady(const VisionFrameResult& result);
    void targetObservationReady(const TargetObservation& observation);
    void detectorInfoReady(const VisionDetectorInfo& info);
    void trackerInfoReady(const VisionTrackerInfo& info);
    void trackingTransition(TargetTrackingStatus status,
                            quint64 frameSequence,
                            const QString& diagnostic);
    void availabilityChanged(bool available, const QString& message);
    void pausedChanged(bool paused);
    void failed(const QString& stage, const QString& message);

private:
    friend class VisionInferenceThread;

    void handleDetectorInitialized(bool available,
                                   const QString& error,
                                   const VisionDetectorInfo& info,
                                   const VisionTrackerInfo& trackerInfo);
    void handleInferenceCompleted(const VisionFrameResult& result,
                                  const QString& error);
    void handleObservationCompleted(const TargetObservation& observation);
    void updateEffectivePause();

    CameraSourcePort* m_cameraSource = nullptr;
    VisionDetectorPort* m_detector = nullptr;
    VisionTrackerPort* m_tracker = nullptr;
    std::unique_ptr<VisionInferenceThread> m_inferenceThread;
    VisionTrackingConfiguration m_trackingConfiguration;
    int m_minimumIntervalMs = 300;
    bool m_running = false;
    bool m_available = false;
    bool m_cameraAcquired = false;
    bool m_manualPaused = false;
    bool m_videoCallActive = false;
    bool m_effectivePaused = false;
};
