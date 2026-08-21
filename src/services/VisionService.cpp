#include "VisionService.h"

#include "platform/VisionAdapter.h"

#include <cmath>
#include <utility>

namespace {
constexpr int PersonCooldownMs = 2'000;
constexpr int FallCandidateCooldownMs = 5'000;
constexpr int FallConfirmedCooldownMs = 30'000;
constexpr int WaveCooldownMs = 3'500;
}

VisionService::VisionService(VisionAdapter* adapter, Clock clock, QObject* parent)
    : QObject(parent),
      m_adapter(adapter),
      m_clock(clock ? std::move(clock) : [] { return QDateTime::currentDateTime(); })
{
    if (!m_adapter) {
        m_status.state = VisionRuntimeState::Degraded;
        m_status.summary = QStringLiteral("视觉 Adapter 未配置，LongPet 已降级运行");
        return;
    }
    m_status = m_adapter->status();
    connect(m_adapter, &VisionAdapter::detectionReceived,
            this, &VisionService::handleDetection);
    connect(m_adapter, &VisionAdapter::statusChanged,
            this, &VisionService::handleRuntimeStatus);
    connect(m_adapter, &VisionAdapter::diagnosticMessage,
            this, &VisionService::adapterDiagnostic);
    connect(m_adapter, &VisionAdapter::recoveryScheduled,
            this, &VisionService::recoveryScheduled);
}

VisionStatus VisionService::status() const
{
    return m_status;
}

bool VisionService::isDetectionValid(const VisionDetection& detection,
                                     QString* reason)
{
    if (detection.type == VisionEventType::Unknown) {
        if (reason)
            *reason = QStringLiteral("未知视觉事件");
        return false;
    }
    if (!std::isfinite(detection.confidence) || detection.confidence < 0.0
        || detection.confidence > 1.0) {
        if (reason)
            *reason = QStringLiteral("视觉事件置信度超出 0..1 范围");
        return false;
    }
    return true;
}

int VisionService::defaultCooldownMs(VisionEventType type)
{
    switch (type) {
    case VisionEventType::PersonDetected:
        return PersonCooldownMs;
    case VisionEventType::FallCandidate:
        return FallCandidateCooldownMs;
    case VisionEventType::FallConfirmed:
        return FallConfirmedCooldownMs;
    case VisionEventType::Wave:
        return WaveCooldownMs;
    case VisionEventType::Unknown:
        break;
    }
    return 0;
}

VisionConfig VisionService::config() const
{
    VisionConfig config;
    if (!m_adapter)
        return config;
    const auto options = m_adapter->options();
    config.enabled = options.enabled;
    config.cameraIndex = options.cameraIndex;
    config.frameWidth = options.frameWidth;
    config.frameHeight = options.frameHeight;
    config.targetFps = options.targetFps;
    config.waveEnabled = options.waveEnabled;
    config.fallCandidateEnabled = options.fallEnabled;
    return config;
}

bool VisionService::setEnabled(bool enabled)
{
    return m_adapter && m_adapter->setEnabled(enabled);
}

bool VisionService::start()
{
    return m_adapter && m_adapter->start();
}

void VisionService::stop()
{
    if (m_adapter)
        m_adapter->stop();
}

bool VisionService::restart()
{
    return m_adapter && m_adapter->restart();
}

bool VisionService::reconfigure(const VisionConfig& config, QString* error)
{
    if (!m_adapter) {
        if (error)
            *error = QStringLiteral("Vision Adapter 未配置");
        return false;
    }
    auto options = m_adapter->options();
    options.enabled = config.enabled;
    options.cameraIndex = config.cameraIndex;
    options.frameWidth = config.frameWidth;
    options.frameHeight = config.frameHeight;
    options.targetFps = config.targetFps;
    options.waveEnabled = config.waveEnabled;
    options.fallEnabled = config.fallCandidateEnabled;
    return m_adapter->reconfigure(options, error);
}

void VisionService::injectDiagnosticDetection(VisionEventType type,
                                              double confidence)
{
    VisionDetection detection;
    detection.type = type;
    detection.confidence = confidence;
    detection.timestamp = m_clock();
    detection.source = QStringLiteral("developer_simulation");
    emit diagnosticInjectionRequested(detection);
    handleDetection(detection);
}

void VisionService::handleDetection(const VisionDetection& detection)
{
    QString invalidReason;
    if (!isDetectionValid(detection, &invalidReason)) {
        emit detectionSuppressed(detection, invalidReason);
        return;
    }

    const QDateTime now = m_clock();
    const int key = static_cast<int>(detection.type);
    const QDateTime previous = m_lastAcceptedAt.value(key);
    const qint64 elapsed = previous.isValid() ? previous.msecsTo(now) : -1;
    const int cooldown = defaultCooldownMs(detection.type);
    if (previous.isValid() && elapsed >= 0 && elapsed < cooldown) {
        emit detectionSuppressed(
            detection,
            QStringLiteral("视觉事件处于 %1 ms 冷却期").arg(cooldown));
        return;
    }

    VisionDetection accepted = detection;
    if (!accepted.timestamp.isValid())
        accepted.timestamp = now;
    m_lastAcceptedAt.insert(key, now);
    m_status.lastEventType = accepted.type;
    m_status.lastEventAt = accepted.timestamp;
    m_status.lastConfidence = accepted.confidence;
    emit statusChanged(m_status);
    emit detectionAccepted(accepted);

    switch (accepted.type) {
    case VisionEventType::PersonDetected:
        emit personDetected(accepted);
        break;
    case VisionEventType::FallCandidate:
        emit fallCandidateDetected(accepted);
        break;
    case VisionEventType::FallConfirmed:
        emit fallConfirmed(accepted);
        break;
    case VisionEventType::Wave:
        emit waveDetected(accepted);
        break;
    case VisionEventType::Unknown:
        break;
    }
}

void VisionService::handleRuntimeStatus(const VisionStatus& status)
{
    const VisionEventType lastEventType = m_status.lastEventType;
    const QDateTime lastEventAt = m_status.lastEventAt;
    const double lastConfidence = m_status.lastConfidence;
    m_status = status;
    m_status.lastEventType = lastEventType;
    m_status.lastEventAt = lastEventAt;
    m_status.lastConfidence = lastConfidence;
    emit statusChanged(m_status);
}
