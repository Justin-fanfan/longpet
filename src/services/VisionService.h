#pragma once

#include "model/VisionModels.h"

#include <QObject>

#include <memory>

class CameraFrame;
class CameraSourcePort;
class VisionDetectorPort;
class VisionInferenceThread;

class VisionService final : public QObject {
    Q_OBJECT

public:
    explicit VisionService(CameraSourcePort* cameraSource,
                           VisionDetectorPort* detector,
                           int minimumIntervalMs = -1,
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
    void detectorInfoReady(const VisionDetectorInfo& info);
    void availabilityChanged(bool available, const QString& message);
    void pausedChanged(bool paused);
    void failed(const QString& stage, const QString& message);

private:
    friend class VisionInferenceThread;

    void handleDetectorInitialized(bool available,
                                   const QString& error,
                                   const VisionDetectorInfo& info);
    void handleInferenceCompleted(const VisionFrameResult& result,
                                  const QString& error);
    void updateEffectivePause();

    CameraSourcePort* m_cameraSource = nullptr;
    VisionDetectorPort* m_detector = nullptr;
    std::unique_ptr<VisionInferenceThread> m_inferenceThread;
    int m_minimumIntervalMs = 300;
    bool m_running = false;
    bool m_available = false;
    bool m_cameraAcquired = false;
    bool m_manualPaused = false;
    bool m_videoCallActive = false;
    bool m_effectivePaused = false;
};
