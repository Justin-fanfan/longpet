#include "VisionService.h"

#include "services/CameraPorts.h"
#include "services/VisionPorts.h"

#include <QElapsedTimer>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QThread>
#include <QWaitCondition>

#include <optional>

namespace {
int configuredIntervalMs()
{
    bool valid = false;
    const int value = qEnvironmentVariableIntValue(
        "LONGPET_VISION_INTERVAL_MS", &valid);
    if (!valid || value < 1 || value > 60'000)
        return 300;
    return value;
}
}

class VisionInferenceThread final : public QThread {
public:
    VisionInferenceThread(VisionDetectorPort* detector,
                          VisionService* service,
                          int minimumIntervalMs)
        : m_detector(detector),
          m_service(service),
          m_minimumIntervalMs(minimumIntervalMs)
    {
    }

    void submit(const CameraFrame& frame)
    {
        QMutexLocker locker(&m_mutex);
        if (m_stopping || m_paused)
            return;
        m_pendingFrame = frame;
        m_waitCondition.wakeOne();
    }

    void setPaused(bool paused)
    {
        QMutexLocker locker(&m_mutex);
        m_paused = paused;
        if (paused)
            m_pendingFrame.reset();
        m_waitCondition.wakeAll();
    }

    void shutdown()
    {
        {
            QMutexLocker locker(&m_mutex);
            m_stopping = true;
            m_pendingFrame.reset();
            m_waitCondition.wakeAll();
        }
        wait();
    }

protected:
    void run() override
    {
        QString initializationError;
        const bool initialized = m_detector
            && m_detector->initialize(&initializationError);
        const VisionDetectorInfo detectorInfo = m_detector
            ? m_detector->info() : VisionDetectorInfo {};
        postInitialization(initialized, initializationError, detectorInfo);
        if (!initialized)
            return;

        QElapsedTimer cadence;
        while (true) {
            CameraFrame frame;
            {
                QMutexLocker locker(&m_mutex);
                while (!m_stopping) {
                    if (m_paused || !m_pendingFrame.has_value()) {
                        m_waitCondition.wait(&m_mutex);
                        continue;
                    }
                    if (cadence.isValid()) {
                        const qint64 remaining = m_minimumIntervalMs
                            - cadence.elapsed();
                        if (remaining > 0) {
                            m_waitCondition.wait(
                                &m_mutex,
                                static_cast<unsigned long>(remaining));
                            continue;
                        }
                    }
                    frame = std::move(*m_pendingFrame);
                    m_pendingFrame.reset();
                    break;
                }
                if (m_stopping)
                    return;
            }

            if (cadence.isValid())
                cadence.restart();
            else
                cadence.start();
            QString inferenceError;
            const VisionFrameResult result = m_detector->detect(
                frame, &inferenceError);
            postResult(result, inferenceError);
        }
    }

private:
    void postInitialization(bool initialized,
                            const QString& error,
                            const VisionDetectorInfo& info)
    {
        const QPointer<VisionService> target = m_service;
        if (!target)
            return;
        QMetaObject::invokeMethod(target, [target, initialized, error, info] {
            if (target) {
                target->handleDetectorInitialized(
                    initialized, error, info);
            }
        }, Qt::QueuedConnection);
    }

    void postResult(const VisionFrameResult& result, const QString& error)
    {
        const QPointer<VisionService> target = m_service;
        if (!target)
            return;
        QMetaObject::invokeMethod(target, [target, result, error] {
            if (target)
                target->handleInferenceCompleted(result, error);
        }, Qt::QueuedConnection);
    }

    VisionDetectorPort* m_detector = nullptr;
    QPointer<VisionService> m_service;
    QMutex m_mutex;
    QWaitCondition m_waitCondition;
    std::optional<CameraFrame> m_pendingFrame;
    int m_minimumIntervalMs = 300;
    bool m_stopping = false;
    bool m_paused = false;
};

VisionService::VisionService(CameraSourcePort* cameraSource,
                             VisionDetectorPort* detector,
                             int minimumIntervalMs,
                             QObject* parent)
    : QObject(parent),
      m_cameraSource(cameraSource),
      m_detector(detector),
      m_minimumIntervalMs(minimumIntervalMs > 0
                              ? minimumIntervalMs : configuredIntervalMs())
{
    qRegisterMetaType<VisionFrameResult>();
    qRegisterMetaType<VisionDetectorInfo>();
    if (m_cameraSource) {
        connect(m_cameraSource, &CameraSourcePort::frameReady, this,
                [this](const CameraFrame& frame) {
            if (m_running && m_available && !m_effectivePaused
                && m_inferenceThread) {
                m_inferenceThread->submit(frame);
            }
        });
        connect(m_cameraSource, &CameraSourcePort::failed, this,
                [this](const QString&, const QString& message) {
            if (!m_cameraAcquired)
                return;
            m_cameraSource->release(this);
            m_cameraAcquired = false;
            m_available = false;
            emit availabilityChanged(false, message);
            emit failed(QStringLiteral("Camera"), message);
        });
    }
}

VisionService::~VisionService()
{
    stop();
}

void VisionService::start()
{
    if (m_running)
        return;
    m_running = true;
    m_available = false;
    if (!m_cameraSource || !m_detector) {
        const QString message = QStringLiteral("视觉依赖未配置");
        emit availabilityChanged(false, message);
        emit failed(QStringLiteral("Initialization"), message);
        return;
    }
    m_inferenceThread = std::make_unique<VisionInferenceThread>(
        m_detector, this, m_minimumIntervalMs);
    m_inferenceThread->setPaused(m_effectivePaused);
    m_inferenceThread->start();
}

void VisionService::stop()
{
    if (!m_running && !m_inferenceThread)
        return;
    m_running = false;
    m_available = false;
    if (m_cameraAcquired && m_cameraSource) {
        m_cameraSource->release(this);
        m_cameraAcquired = false;
    }
    if (m_inferenceThread) {
        m_inferenceThread->shutdown();
        m_inferenceThread.reset();
    }
}

bool VisionService::isRunning() const
{
    return m_running;
}

bool VisionService::isAvailable() const
{
    return m_available;
}

bool VisionService::isPaused() const
{
    return m_effectivePaused;
}

int VisionService::minimumIntervalMs() const
{
    return m_minimumIntervalMs;
}

void VisionService::setPaused(bool paused)
{
    if (m_manualPaused == paused)
        return;
    m_manualPaused = paused;
    updateEffectivePause();
}

void VisionService::setVideoCallActive(bool active)
{
    if (m_videoCallActive == active)
        return;
    m_videoCallActive = active;
    updateEffectivePause();
}

void VisionService::handleDetectorInitialized(
    bool available, const QString& error, const VisionDetectorInfo& info)
{
    if (!m_running)
        return;
    emit detectorInfoReady(info);
    if (!available) {
        const QString message = error.isEmpty()
            ? QStringLiteral("FastestDet 不可用") : error;
        emit availabilityChanged(false, message);
        emit failed(QStringLiteral("Model"), message);
        return;
    }

    QString cameraError;
    if (!m_cameraSource->acquire(this, &cameraError)) {
        const QString message = cameraError.isEmpty()
            ? QStringLiteral("共享摄像头不可用") : cameraError;
        emit availabilityChanged(false, message);
        emit failed(QStringLiteral("Camera"), message);
        return;
    }
    m_cameraAcquired = true;
    m_available = true;
    const QString message = m_effectivePaused
        ? QStringLiteral("视觉服务已就绪，当前已暂停")
        : QStringLiteral("视觉服务已就绪");
    emit availabilityChanged(true, message);
}

void VisionService::handleInferenceCompleted(
    const VisionFrameResult& result, const QString& error)
{
    if (!m_running || !m_available || m_effectivePaused)
        return;
    if (!error.isEmpty()) {
        emit failed(QStringLiteral("Inference"), error);
        return;
    }
    emit visionResultReady(result);
}

void VisionService::updateEffectivePause()
{
    const bool paused = m_manualPaused || m_videoCallActive;
    if (m_effectivePaused == paused)
        return;
    m_effectivePaused = paused;
    if (m_inferenceThread)
        m_inferenceThread->setPaused(paused);
    emit pausedChanged(paused);
}
