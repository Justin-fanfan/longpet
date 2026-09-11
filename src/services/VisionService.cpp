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

#include <algorithm>
#include <cmath>
#include <optional>

namespace {
int configuredInteger(const char* name, int fallback,
                      int minimum, int maximum)
{
    bool valid = false;
    const int value = qEnvironmentVariableIntValue(name, &valid);
    return valid && value >= minimum && value <= maximum ? value : fallback;
}

double configuredDouble(const char* name, double fallback,
                        double minimum, double maximum)
{
    bool valid = false;
    const double value = qEnvironmentVariable(name).toDouble(&valid);
    return valid && value >= minimum && value <= maximum ? value : fallback;
}

bool configuredBoolean(const char* name, bool fallback)
{
    if (!qEnvironmentVariableIsSet(name))
        return fallback;
    const QString value = qEnvironmentVariable(name).trimmed().toLower();
    return value == QStringLiteral("1") || value == QStringLiteral("true")
        || value == QStringLiteral("yes") || value == QStringLiteral("on");
}

VisionTrackingConfiguration detectorOnlyConfiguration(int intervalMs)
{
    VisionTrackingConfiguration configuration =
        VisionTrackingConfiguration::fromEnvironment();
    configuration.trackingEnabled = false;
    if (intervalMs > 0) {
        configuration.searchDetectorIntervalMs = intervalMs;
        configuration.lostDetectorIntervalMs = intervalMs;
    }
    return configuration;
}

QRectF normalizedRect(const PersonDetection& detection)
{
    return QRectF(detection.normalizedCenter.x()
                      - detection.normalizedSize.width() * 0.5,
                  detection.normalizedCenter.y()
                      - detection.normalizedSize.height() * 0.5,
                  detection.normalizedSize.width(),
                  detection.normalizedSize.height());
}

double intersectionOverUnion(const PersonDetection& first,
                             const PersonDetection& second)
{
    const QRectF overlap = normalizedRect(first).intersected(
        normalizedRect(second));
    const double intersection = overlap.width() * overlap.height();
    const QRectF firstRect = normalizedRect(first);
    const QRectF secondRect = normalizedRect(second);
    const double unionArea = firstRect.width() * firstRect.height()
        + secondRect.width() * secondRect.height() - intersection;
    return unionArea > 0.0 ? intersection / unionArea : 0.0;
}

std::optional<PersonDetection> selectTarget(
    const QList<PersonDetection>& candidates,
    const std::optional<PersonDetection>& previous)
{
    if (candidates.isEmpty())
        return std::nullopt;
    if (!previous.has_value()) {
        return *std::max_element(
            candidates.cbegin(), candidates.cend(),
            [](const PersonDetection& first, const PersonDetection& second) {
                const double firstScore = first.confidence
                    + first.normalizedSize.width()
                        * first.normalizedSize.height() * 0.1;
                const double secondScore = second.confidence
                    + second.normalizedSize.width()
                        * second.normalizedSize.height() * 0.1;
                return firstScore < secondScore;
            });
    }

    const PersonDetection* best = nullptr;
    double bestScore = -1.0;
    double bestIou = 0.0;
    double bestDistance = 2.0;
    for (const PersonDetection& candidate : candidates) {
        const double iou = intersectionOverUnion(*previous, candidate);
        const double dx = candidate.normalizedCenter.x()
            - previous->normalizedCenter.x();
        const double dy = candidate.normalizedCenter.y()
            - previous->normalizedCenter.y();
        const double distance = std::sqrt(dx * dx + dy * dy);
        const double score = iou * 2.0 + std::max(0.0, 1.0 - distance)
            + candidate.confidence * 0.25;
        if (score > bestScore) {
            best = &candidate;
            bestScore = score;
            bestIou = iou;
            bestDistance = distance;
        }
    }
    // Periodic correction must not jump to a distant second person. Once the
    // original target is truly lost, SEARCHING may select another person.
    if (!best || (bestIou < 0.05 && bestDistance > 0.35))
        return std::nullopt;
    return *best;
}
}

VisionTrackingConfiguration VisionTrackingConfiguration::fromEnvironment()
{
    VisionTrackingConfiguration configuration;
    configuration.trackingEnabled = configuredBoolean(
        "LONGPET_VISION_TRACKING_ENABLED", true);
    configuration.trackerIntervalMs = configuredInteger(
        "LONGPET_VISION_TRACKER_INTERVAL_MS", 100, 20, 5'000);
    configuration.detectorCorrectionIntervalMs = configuredInteger(
        "LONGPET_VISION_DETECTOR_CORRECTION_MS", 8'000, 1'000, 120'000);
    configuration.searchDetectorIntervalMs = configuredInteger(
        "LONGPET_VISION_SEARCH_INTERVAL_MS", 250, 1, 60'000);
    configuration.lostDetectorIntervalMs = configuredInteger(
        "LONGPET_VISION_LOST_INTERVAL_MS", 250, 1, 60'000);
    configuration.freshnessTimeoutMs = configuredInteger(
        "LONGPET_VISION_FRESHNESS_MS", 2'500, 100, 60'000);
    configuration.minimumTrackerConfidence = configuredDouble(
        "LONGPET_VISION_TRACKER_MIN_CONFIDENCE", 0.15, 0.0, 1.0);
    configuration.lowConfidenceFrameLimit = configuredInteger(
        "LONGPET_VISION_TRACKER_LOW_CONFIDENCE_FRAMES", 3, 1, 30);
    return configuration;
}

class VisionInferenceThread final : public QThread {
public:
    VisionInferenceThread(VisionDetectorPort* detector,
                          VisionTrackerPort* tracker,
                          VisionTrackingConfiguration configuration,
                          VisionService* service)
        : m_detector(detector),
          m_tracker(configuration.trackingEnabled ? tracker : nullptr),
          m_configuration(configuration),
          m_service(service)
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
        m_resetRequested = true;
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
        const VisionTrackerInfo trackerInfo = m_tracker
            ? m_tracker->info() : VisionTrackerInfo {};
        postInitialization(initialized, initializationError,
                           detectorInfo, trackerInfo);
        if (!initialized)
            return;

        m_runtime.start();
        while (true) {
            CameraFrame frame;
            bool shouldReset = false;
            {
                QMutexLocker locker(&m_mutex);
                while (!m_stopping) {
                    if (m_resetRequested) {
                        m_resetRequested = false;
                        shouldReset = true;
                        break;
                    }
                    if (m_paused || !m_pendingFrame.has_value()) {
                        m_waitCondition.wait(&m_mutex);
                        continue;
                    }
                    const int interval = processingIntervalMs();
                    if (m_stepCadence.isValid()) {
                        const qint64 remaining = interval
                            - m_stepCadence.elapsed();
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
                    break;
            }
            if (shouldReset) {
                resetTracking();
                continue;
            }
            if (!frame.isValid())
                continue;
            if (m_stepCadence.isValid())
                m_stepCadence.restart();
            else
                m_stepCadence.start();
            process(frame);
        }
        resetTracking();
    }

private:
    enum class Mode { Searching, Tracking, Lost };

    int processingIntervalMs() const
    {
        if (m_mode == Mode::Tracking)
            return m_configuration.trackerIntervalMs;
        if (m_mode == Mode::Lost)
            return m_configuration.lostDetectorIntervalMs;
        return m_configuration.searchDetectorIntervalMs;
    }

    void resetTracking()
    {
        if (m_tracker)
            m_tracker->reset();
        m_mode = Mode::Searching;
        m_currentTarget.reset();
        m_lastDetectorFinishedMs = -1;
        m_lastPublishedStatus.reset();
        m_lowConfidenceFrames = 0;
    }

    void process(const CameraFrame& frame)
    {
        if (!m_tracker) {
            runDetectorOnly(frame);
            return;
        }
        if (m_mode == Mode::Tracking) {
            const bool correctionDue = m_lastDetectorFinishedMs < 0
                || m_runtime.elapsed() - m_lastDetectorFinishedMs
                    >= m_configuration.detectorCorrectionIntervalMs;
            if (correctionDue)
                runCorrection(frame);
            else
                runTracker(frame, false, 0.0);
            return;
        }
        runSearch(frame);
    }

    VisionFrameResult detect(const CameraFrame& frame, QString* error)
    {
        VisionFrameResult result = m_detector->detect(frame, error);
        m_lastDetectorFinishedMs = m_runtime.elapsed();
        postDetectorResult(result, *error);
        return result;
    }

    void runDetectorOnly(const CameraFrame& frame)
    {
        QString error;
        const VisionFrameResult result = detect(frame, &error);
        const auto target = error.isEmpty()
            ? selectTarget(result.persons, std::nullopt) : std::nullopt;
        publish(frame, result.sourceSize, target,
                target.has_value() ? TargetTrackingStatus::Detected
                                   : TargetTrackingStatus::Searching,
                true, result.totalMs, 0.0, 0.0F, 0, error);
    }

    void runSearch(const CameraFrame& frame)
    {
        QString error;
        const VisionFrameResult result = detect(frame, &error);
        if (!error.isEmpty()) {
            publish(frame, result.sourceSize, std::nullopt,
                    TargetTrackingStatus::Searching, true,
                    result.totalMs, 0.0, 0.0F, 0, error);
            return;
        }
        const auto target = selectTarget(result.persons, std::nullopt);
        if (!target.has_value()) {
            publish(frame, result.sourceSize, std::nullopt,
                    TargetTrackingStatus::Searching, true,
                    result.totalMs, 0.0, 0.0F, 0, {});
            return;
        }

        QString trackerError;
        const bool trackerStarted = m_tracker->start(
            frame, *target, &trackerError);
        const TargetTrackingStatus status = m_hadTarget
            ? TargetTrackingStatus::Reacquired
            : TargetTrackingStatus::Detected;
        m_currentTarget = *target;
        m_hadTarget = true;
        publish(frame, result.sourceSize, target, status, true,
                result.totalMs, 0.0, 0.0F, 0, trackerError);
        if (trackerStarted) {
            m_mode = Mode::Tracking;
            m_lowConfidenceFrames = 0;
        } else {
            m_mode = Mode::Lost;
        }
    }

    void runCorrection(const CameraFrame& frame)
    {
        QString detectorError;
        const VisionFrameResult detection = detect(frame, &detectorError);
        const auto corrected = detectorError.isEmpty()
            ? selectTarget(detection.persons, m_currentTarget) : std::nullopt;
        if (corrected.has_value()) {
            QString trackerError;
            if (m_tracker->start(frame, *corrected, &trackerError)) {
                m_currentTarget = *corrected;
                m_lowConfidenceFrames = 0;
                publish(frame, detection.sourceSize, corrected,
                        TargetTrackingStatus::Corrected, true,
                        detection.totalMs, 0.0, 0.0F, 0, {});
                return;
            }
        }
        // A detector miss does not immediately discard a healthy track.
        runTracker(frame, true, detection.totalMs,
                   detectorError.isEmpty()
                       ? QStringLiteral("周期检测未匹配当前目标")
                       : detectorError);
    }

    void runTracker(const CameraFrame& frame, bool detectorRan,
                    double detectorMs, const QString& diagnostic = {})
    {
        QString trackerError;
        const VisionTrackerResult tracked = m_tracker->update(
            frame, &trackerError);
        if (!tracked.success) {
            m_tracker->reset();
            m_mode = Mode::Lost;
            m_currentTarget.reset();
            publish(frame, tracked.sourceSize, std::nullopt,
                    TargetTrackingStatus::Lost, detectorRan,
                    detectorMs, tracked.totalMs, 0.0F,
                    tracked.trackedPointCount,
                    trackerError.isEmpty() ? diagnostic : trackerError);
            return;
        }
        if (tracked.confidence < m_configuration.minimumTrackerConfidence)
            ++m_lowConfidenceFrames;
        else
            m_lowConfidenceFrames = 0;
        if (m_lowConfidenceFrames
            >= m_configuration.lowConfidenceFrameLimit) {
            m_tracker->reset();
            m_mode = Mode::Lost;
            m_currentTarget.reset();
            publish(frame, tracked.sourceSize, std::nullopt,
                    TargetTrackingStatus::Lost, detectorRan,
                    detectorMs, tracked.totalMs, tracked.confidence,
                    tracked.trackedPointCount,
                    QStringLiteral("跟踪可信度连续 %1 帧低于 %2")
                        .arg(m_lowConfidenceFrames)
                        .arg(m_configuration.minimumTrackerConfidence,
                             0, 'f', 2));
            m_lowConfidenceFrames = 0;
            return;
        }
        m_currentTarget = tracked.target;
        publish(frame, tracked.sourceSize, tracked.target,
                TargetTrackingStatus::Tracking, detectorRan,
                detectorMs, tracked.totalMs, tracked.confidence,
                tracked.trackedPointCount, diagnostic);
    }

    void publish(const CameraFrame& frame, const QSize& sourceSize,
                 const std::optional<PersonDetection>& target,
                 TargetTrackingStatus status, bool detectorRan,
                 double detectorMs, double trackerMs,
                 float trackerConfidence, int trackedPointCount,
                 const QString& diagnostic)
    {
        TargetObservation observation;
        observation.present = target.has_value();
        observation.status = status;
        observation.frameSequence = frame.sequence;
        observation.timestamp = frame.timestamp;
        observation.publishedAt = QDateTime::currentDateTimeUtc();
        observation.ageMs = frame.timestamp.isValid()
            ? std::max<qint64>(0, frame.timestamp.msecsTo(
                  observation.publishedAt)) : 0;
        observation.fresh = frame.timestamp.isValid()
            && observation.ageMs <= m_configuration.freshnessTimeoutMs;
        observation.sourceSize = sourceSize;
        if (target.has_value()) {
            observation.target = *target;
            observation.detectorConfidence = target->confidence;
        }
        observation.trackerConfidence = trackerConfidence;
        observation.trackerName = m_tracker
            ? m_tracker->info().trackerName : QString {};
        observation.trackedPointCount = trackedPointCount;
        observation.detectorRan = detectorRan;
        observation.detectorMs = detectorMs;
        observation.trackerMs = trackerMs;
        observation.diagnostic = diagnostic;
        postObservation(observation);
    }

    void postInitialization(bool initialized, const QString& error,
                            const VisionDetectorInfo& detectorInfo,
                            const VisionTrackerInfo& trackerInfo)
    {
        const QPointer<VisionService> target = m_service;
        if (!target)
            return;
        QMetaObject::invokeMethod(
            target,
            [target, initialized, error, detectorInfo, trackerInfo] {
                if (target) {
                    target->handleDetectorInitialized(
                        initialized, error, detectorInfo, trackerInfo);
                }
            }, Qt::QueuedConnection);
    }

    void postDetectorResult(const VisionFrameResult& result,
                            const QString& error)
    {
        const QPointer<VisionService> target = m_service;
        if (!target)
            return;
        QMetaObject::invokeMethod(target, [target, result, error] {
            if (target)
                target->handleInferenceCompleted(result, error);
        }, Qt::QueuedConnection);
    }

    void postObservation(const TargetObservation& observation)
    {
        const bool transition = !m_lastPublishedStatus.has_value()
            || *m_lastPublishedStatus != observation.status
            || observation.status == TargetTrackingStatus::Corrected
            || observation.status == TargetTrackingStatus::Reacquired;
        m_lastPublishedStatus = observation.status;
        const QPointer<VisionService> target = m_service;
        if (!target)
            return;
        QMetaObject::invokeMethod(target, [target, observation, transition] {
            if (!target)
                return;
            target->handleObservationCompleted(observation);
            if (transition) {
                emit target->trackingTransition(
                    observation.status, observation.frameSequence,
                    observation.diagnostic);
            }
        }, Qt::QueuedConnection);
    }

    VisionDetectorPort* m_detector = nullptr;
    VisionTrackerPort* m_tracker = nullptr;
    VisionTrackingConfiguration m_configuration;
    QPointer<VisionService> m_service;
    QMutex m_mutex;
    QWaitCondition m_waitCondition;
    std::optional<CameraFrame> m_pendingFrame;
    std::optional<PersonDetection> m_currentTarget;
    std::optional<TargetTrackingStatus> m_lastPublishedStatus;
    QElapsedTimer m_runtime;
    QElapsedTimer m_stepCadence;
    qint64 m_lastDetectorFinishedMs = -1;
    Mode m_mode = Mode::Searching;
    bool m_hadTarget = false;
    bool m_stopping = false;
    bool m_paused = false;
    bool m_resetRequested = false;
    int m_lowConfidenceFrames = 0;
};

VisionService::VisionService(CameraSourcePort* cameraSource,
                             VisionDetectorPort* detector,
                             int minimumIntervalMs,
                             QObject* parent)
    : VisionService(cameraSource, detector, nullptr,
                    detectorOnlyConfiguration(minimumIntervalMs), parent)
{
}

VisionService::VisionService(
    CameraSourcePort* cameraSource, VisionDetectorPort* detector,
    VisionTrackerPort* tracker, VisionTrackingConfiguration configuration,
    QObject* parent)
    : QObject(parent),
      m_cameraSource(cameraSource),
      m_detector(detector),
      m_tracker(tracker),
      m_trackingConfiguration(configuration),
      m_minimumIntervalMs(configuration.searchDetectorIntervalMs)
{
    qRegisterMetaType<VisionFrameResult>();
    qRegisterMetaType<VisionDetectorInfo>();
    qRegisterMetaType<VisionTrackerInfo>();
    qRegisterMetaType<TargetTrackingStatus>();
    qRegisterMetaType<TargetObservation>();
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

VisionService::~VisionService() { stop(); }

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
        m_detector, m_tracker, m_trackingConfiguration, this);
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

bool VisionService::isRunning() const { return m_running; }
bool VisionService::isAvailable() const { return m_available; }
bool VisionService::isPaused() const { return m_effectivePaused; }
int VisionService::minimumIntervalMs() const { return m_minimumIntervalMs; }

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
    bool available, const QString& error, const VisionDetectorInfo& info,
    const VisionTrackerInfo& trackerInfo)
{
    if (!m_running)
        return;
    emit detectorInfoReady(info);
    if (!trackerInfo.trackerName.isEmpty())
        emit trackerInfoReady(trackerInfo);
    if (!available) {
        const QString message = error.isEmpty()
            ? QStringLiteral("视觉检测器不可用") : error;
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
        emit failed(QStringLiteral("Detector"), error);
        return;
    }
    emit visionResultReady(result);
}

void VisionService::handleObservationCompleted(
    const TargetObservation& observation)
{
    if (m_running && m_available && !m_effectivePaused)
        emit targetObservationReady(observation);
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
