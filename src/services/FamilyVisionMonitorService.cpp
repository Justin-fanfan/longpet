#include "FamilyVisionMonitorService.h"

#include "services/CameraPorts.h"
#include "services/FamilyVisionPorts.h"
#include "services/VisionService.h"

#include <algorithm>

FamilyVisionMonitorService::FamilyVisionMonitorService(
    CameraSourcePort* cameraSource, VisionService* visionService,
    FamilyVisionStreamPort* streamPort, int frameRate, QObject* parent)
    : QObject(parent),
      m_cameraSource(cameraSource),
      m_visionService(visionService),
      m_streamPort(streamPort),
      m_frameRate(frameRate > 0 ? std::clamp(frameRate, 1, 10)
                                : configuredFrameRate())
{
    if (m_cameraSource) {
        connect(m_cameraSource, &CameraSourcePort::frameReady,
                this, &FamilyVisionMonitorService::handleCameraFrame);
        connect(m_cameraSource, &CameraSourcePort::failed, this,
                [this](const QString& code, const QString& message) {
            if (m_cameraAcquired && m_streamPort)
                m_streamPort->rejectViewer({}, code, message);
        });
    }
    if (m_visionService) {
        connect(m_visionService, &VisionService::targetObservationReady,
                this, &FamilyVisionMonitorService::handleTargetObservation);
        connect(m_visionService, &VisionService::detectorInfoReady, this,
                [this](const VisionDetectorInfo& info) {
            m_latestTelemetry.detectorName = info.detectorName;
        });
        connect(m_visionService, &VisionService::trackerInfoReady, this,
                [this](const VisionTrackerInfo& info) {
            m_latestTelemetry.trackerName = info.trackerName;
        });
    }
    if (m_streamPort) {
        connect(m_streamPort, &FamilyVisionStreamPort::viewerStartRequested,
                this, &FamilyVisionMonitorService::handleViewerStartRequested);
        connect(m_streamPort, &FamilyVisionStreamPort::viewerStopped,
                this, &FamilyVisionMonitorService::handleViewerStopped);
    }
}

FamilyVisionMonitorService::~FamilyVisionMonitorService()
{
    stop();
}

int FamilyVisionMonitorService::configuredFrameRate()
{
    bool valid = false;
    const int value = qEnvironmentVariableIntValue(
        "LONGPET_VISION_MONITOR_FPS", &valid);
    return valid ? std::clamp(value, 1, 10) : 7;
}

bool FamilyVisionMonitorService::start(QHostAddress address, quint16 port,
                                       QString* error)
{
    if (!m_streamPort) {
        if (error)
            *error = QStringLiteral("AI 视野网络适配器未配置");
        return false;
    }
    if (m_started)
        return true;
    m_started = m_streamPort->start(address, port, error);
    return m_started;
}

void FamilyVisionMonitorService::stop()
{
    releaseCamera();
    if (m_streamPort)
        m_streamPort->stop();
    m_started = false;
    m_frameClock.invalidate();
    m_previousObservationTime = {};
    m_targetUpdateHz = 0.0;
}

FamilyVisionSession FamilyVisionMonitorService::createSession(QString* error)
{
    if (!m_started || !m_streamPort) {
        if (error)
            *error = QStringLiteral("AI 视野服务未启动");
        return {};
    }
    return m_streamPort->createSession(m_frameRate, error);
}

bool FamilyVisionMonitorService::isAvailable() const
{
    return m_started && m_streamPort;
}

bool FamilyVisionMonitorService::hasViewer() const
{
    return m_streamPort && m_streamPort->hasViewer();
}

int FamilyVisionMonitorService::frameRate() const
{
    return m_frameRate;
}

void FamilyVisionMonitorService::handleViewerStartRequested(
    const QString& sessionId)
{
    if (!m_cameraSource || !m_streamPort) {
        if (m_streamPort) {
            m_streamPort->rejectViewer(sessionId,
                QStringLiteral("CAMERA_UNAVAILABLE"),
                QStringLiteral("共享摄像头服务未配置"));
        }
        return;
    }

    QString error;
    if (!m_cameraSource->acquire(this, &error)) {
        m_streamPort->rejectViewer(sessionId,
            QStringLiteral("CAMERA_UNAVAILABLE"),
            error.isEmpty() ? QStringLiteral("摄像头当前不可用") : error);
        return;
    }
    m_cameraAcquired = true;
    m_frameClock.invalidate();
    m_streamPort->acceptViewer(sessionId);

    if (m_latestTelemetry.observation.publishedAt.isValid()) {
        TargetObservation observation = m_latestTelemetry.observation;
        observation.ageMs = std::max<qint64>(
            observation.ageMs,
            observation.timestamp.msecsTo(QDateTime::currentDateTime()));
        if (observation.ageMs > 1'000)
            observation.fresh = false;
        m_latestTelemetry.observation = observation;
        m_streamPort->publishTelemetry(m_latestTelemetry);
    }
}

void FamilyVisionMonitorService::handleViewerStopped(const QString& reason)
{
    Q_UNUSED(reason)
    releaseCamera();
}

void FamilyVisionMonitorService::handleCameraFrame(const CameraFrame& frame)
{
    if (!m_cameraAcquired || !m_streamPort || !m_streamPort->hasViewer()
        || !frame.isValid()) {
        return;
    }
    const int intervalMs = std::max(1, 1'000 / m_frameRate);
    if (m_frameClock.isValid() && m_frameClock.elapsed() < intervalMs)
        return;
    if (m_frameClock.isValid())
        m_frameClock.restart();
    else
        m_frameClock.start();
    m_streamPort->publishCameraFrame(frame);
}

void FamilyVisionMonitorService::handleTargetObservation(
    const TargetObservation& observation)
{
    const QDateTime sampleTime = observation.publishedAt.isValid()
        ? observation.publishedAt : QDateTime::currentDateTime();
    if (m_previousObservationTime.isValid()) {
        const qint64 intervalMs = m_previousObservationTime.msecsTo(sampleTime);
        if (intervalMs > 0 && intervalMs < 5'000) {
            const double instantaneous = 1'000.0 / intervalMs;
            m_targetUpdateHz = m_targetUpdateHz <= 0.0
                ? instantaneous : (m_targetUpdateHz * 0.8 + instantaneous * 0.2);
        }
    }
    m_previousObservationTime = sampleTime;
    m_latestTelemetry.observation = observation;
    m_latestTelemetry.targetUpdateHz = m_targetUpdateHz;
    if (m_latestTelemetry.trackerName.isEmpty())
        m_latestTelemetry.trackerName = observation.trackerName;

    if (m_cameraAcquired && m_streamPort && m_streamPort->hasViewer())
        m_streamPort->publishTelemetry(m_latestTelemetry);
}

void FamilyVisionMonitorService::releaseCamera()
{
    if (m_cameraAcquired && m_cameraSource)
        m_cameraSource->release(this);
    m_cameraAcquired = false;
    m_frameClock.invalidate();
}
