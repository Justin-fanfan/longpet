#include "AutomaticHeadTrackingService.h"

#include "services/MotionService.h"

#include <QDebug>

#include <algorithm>
#include <cmath>

namespace {
qreal clampUnit(qreal value)
{
    return std::clamp(value, 0.0, 1.0);
}
}

bool HeadTrackingGeometry::isControllableStatus(TargetTrackingStatus status)
{
    return status == TargetTrackingStatus::Detected
        || status == TargetTrackingStatus::Tracking
        || status == TargetTrackingStatus::Corrected
        || status == TargetTrackingStatus::Reacquired;
}

bool HeadTrackingGeometry::targetFromObservation(
    const TargetObservation& observation, MotionTargetFrame* target,
    QString* error)
{
    if (!target || !observation.sourceSize.isValid()
        || observation.sourceSize.width() <= 0
        || observation.sourceSize.height() <= 0) {
        if (error)
            *error = QStringLiteral("视觉源尺寸无效");
        return false;
    }
    if (!observation.present || !isControllableStatus(observation.status)) {
        if (error)
            *error = QStringLiteral("当前 observation 没有可控制目标");
        return false;
    }

    const qreal sourceWidth = observation.sourceSize.width();
    const qreal sourceHeight = observation.sourceSize.height();
    qreal centerX = observation.target.normalizedCenter.x();
    qreal centerY = observation.target.normalizedCenter.y();
    qreal width = observation.target.normalizedSize.width();
    qreal height = observation.target.normalizedSize.height();
    const bool normalizedValid = std::isfinite(centerX)
        && std::isfinite(centerY) && std::isfinite(width)
        && std::isfinite(height) && width > 0.0 && height > 0.0;
    if (normalizedValid) {
        centerX = clampUnit(centerX) * sourceWidth;
        centerY = clampUnit(centerY) * sourceHeight;
        width = clampUnit(width) * sourceWidth;
        height = clampUnit(height) * sourceHeight;
    } else {
        const QRectF clipped = observation.target.boundingBox.intersected(
            QRectF(0.0, 0.0, sourceWidth, sourceHeight));
        if (!clipped.isValid() || clipped.isEmpty()) {
            if (error)
                *error = QStringLiteral("人物 bbox 无效");
            return false;
        }
        centerX = clipped.center().x();
        centerY = clipped.center().y();
        width = clipped.width();
        height = clipped.height();
    }

    const qint64 area = std::llround(width * height);
    const qint64 dx = std::llround(centerX - sourceWidth * 0.5);
    const qint64 dy = std::llround(centerY - sourceHeight * 0.5);
    if (dx < -4'096 || dx > 4'096 || dy < -4'096 || dy > 4'096
        || area <= 0 || area > 16'777'216) {
        if (error)
            *error = QStringLiteral("TARGET 超出 Motion Protocol V2 范围");
        return false;
    }
    target->dx = static_cast<int>(dx);
    target->dy = static_cast<int>(dy);
    target->area = static_cast<int>(area);
    return true;
}

AutomaticHeadTrackingService::AutomaticHeadTrackingService(
    MotionService* motionService,
    AutomaticHeadTrackingConfiguration configuration,
    QObject* parent)
    : QObject(parent),
      m_motionService(motionService),
      m_configuration(configuration)
{
    qRegisterMetaType<AutomaticHeadTrackingSnapshot>();
    m_configuration.maximumTargetAgeMs = std::clamp(
        m_configuration.maximumTargetAgeMs, 100, 2'500);
    m_configuration.targetExpiryMs = std::clamp(
        m_configuration.targetExpiryMs, 200, 2'500);
    m_configuration.targetLogIntervalMs = std::max(
        250, m_configuration.targetLogIntervalMs);
    m_targetExpiryTimer.setSingleShot(true);
    m_targetExpiryTimer.setInterval(m_configuration.targetExpiryMs);
    connect(&m_targetExpiryTimer, &QTimer::timeout, this, [this] {
        publishLost(AutomaticHeadTrackingState::Searching,
                    QStringLiteral("target observation timeout"));
    });
    if (m_motionService) {
        m_motionStatus = m_motionService->status();
        connect(m_motionService, &MotionService::statusChanged,
                this, &AutomaticHeadTrackingService::handleMotionStatus);
        connect(m_motionService, &MotionService::manualControlChanged,
                this,
                &AutomaticHeadTrackingService::handleManualControlChanged);
        connect(m_motionService,
                &MotionService::automaticHeadControlChanged,
                this,
                &AutomaticHeadTrackingService::handleAutomaticControlChanged);
    }
    m_snapshot.updatedAt = QDateTime::currentDateTimeUtc();
}

AutomaticHeadTrackingSnapshot AutomaticHeadTrackingService::snapshot() const
{
    return m_snapshot;
}

bool AutomaticHeadTrackingService::setEnabled(bool enabled, QString* error)
{
    if (!m_motionService) {
        if (error)
            *error = QStringLiteral("MotionService 未配置");
        return false;
    }
    if (m_snapshot.enabled == enabled)
        return true;
    m_snapshot.enabled = enabled;
    m_hasConsumedFrame = false;
    m_lastConsumedFrameSequence = 0;
    m_targetExpiryTimer.stop();
    m_lostPublished = true;
    if (!enabled) {
        m_motionService->endAutomaticHeadControl(
            QStringLiteral("AUTO_HEAD disabled"));
        m_snapshot.active = false;
        updateState(AutomaticHeadTrackingState::Disabled,
                    QStringLiteral("自动跟随头部已关闭"));
        qInfo().noquote() << "AUTO_HEAD disabled";
        return true;
    }
    qInfo().noquote() << "AUTO_HEAD enabled";
    reevaluate();
    return true;
}

void AutomaticHeadTrackingService::handleTargetObservation(
    const TargetObservation& observation)
{
    m_snapshot.visionStatus = observation.status;
    m_snapshot.targetAgeMs = currentAgeMs(observation);
    m_snapshot.frameSequence = observation.frameSequence;
    if (!m_snapshot.enabled || m_videoCallActive || m_manualOverride
        || m_visionPaused || !m_visionAvailable) {
        return;
    }
    reevaluate();
    if (!m_snapshot.active)
        return;
    if (m_hasConsumedFrame
        && observation.frameSequence == m_lastConsumedFrameSequence) {
        return;
    }
    m_hasConsumedFrame = true;
    m_lastConsumedFrameSequence = observation.frameSequence;

    if (!observation.fresh
        || m_snapshot.targetAgeMs > m_configuration.maximumTargetAgeMs) {
        qInfo().noquote() << "AUTO_HEAD stale target dropped frame="
                          << observation.frameSequence
                          << "age_ms=" << m_snapshot.targetAgeMs;
        publishLost(AutomaticHeadTrackingState::Searching,
                    QStringLiteral("stale target dropped"));
        return;
    }
    if (!observation.present
        || !HeadTrackingGeometry::isControllableStatus(observation.status)) {
        publishLost(AutomaticHeadTrackingState::Searching,
                    QStringLiteral("target lost"));
        return;
    }

    MotionTargetFrame target;
    QString geometryError;
    if (!HeadTrackingGeometry::targetFromObservation(
            observation, &target, &geometryError)) {
        publishLost(AutomaticHeadTrackingState::Searching, geometryError);
        return;
    }
    QString motionError;
    if (!m_motionService->sendAutomaticTarget(target, &motionError)) {
        m_snapshot.active = false;
        updateState(AutomaticHeadTrackingState::WaitingForMotion,
                    motionError.isEmpty()
                        ? QStringLiteral("TARGET 发送失败") : motionError);
        return;
    }
    m_lostPublished = false;
    m_snapshot.dx = target.dx;
    m_snapshot.dy = target.dy;
    m_snapshot.area = target.area;
    m_targetExpiryTimer.start();
    const bool stateChanged =
        m_snapshot.state != AutomaticHeadTrackingState::Tracking
        || m_snapshot.detail != QStringLiteral("正在自动跟随头部");
    updateState(AutomaticHeadTrackingState::Tracking,
                QStringLiteral("正在自动跟随头部"));
    if (!stateChanged)
        publishSnapshot();
    if (!m_targetLogTimer.isValid()
        || m_targetLogTimer.elapsed() >= m_configuration.targetLogIntervalMs) {
        qInfo().noquote() << "AUTO_HEAD target"
                          << "dx=" << target.dx
                          << "dy=" << target.dy
                          << "area=" << target.area
                          << "age_ms=" << m_snapshot.targetAgeMs
                          << "frame=" << observation.frameSequence;
        if (m_targetLogTimer.isValid())
            m_targetLogTimer.restart();
        else
            m_targetLogTimer.start();
    }
}

void AutomaticHeadTrackingService::setVisionAvailable(
    bool available, const QString& detail)
{
    m_visionAvailable = available;
    if (!available && m_motionService) {
        m_motionService->endAutomaticHeadControl(
            detail.isEmpty() ? QStringLiteral("Vision unavailable") : detail);
        m_snapshot.active = false;
    }
    reevaluate();
}

void AutomaticHeadTrackingService::setVisionPaused(bool paused)
{
    if (m_visionPaused == paused)
        return;
    m_visionPaused = paused;
    if (paused && m_motionService) {
        m_targetExpiryTimer.stop();
        m_motionService->endAutomaticHeadControl(
            m_videoCallActive ? QStringLiteral("video-call suspend")
                              : QStringLiteral("Vision paused"));
        m_snapshot.active = false;
    }
    reevaluate();
}

void AutomaticHeadTrackingService::setVideoCallActive(bool active)
{
    if (m_videoCallActive == active)
        return;
    m_videoCallActive = active;
    if (active && m_motionService) {
        m_targetExpiryTimer.stop();
        m_motionService->endAutomaticHeadControl(
            QStringLiteral("video-call suspend"));
        m_snapshot.active = false;
        qInfo().noquote() << "AUTO_HEAD video-call suspend";
    } else if (!active) {
        qInfo().noquote() << "AUTO_HEAD video-call resume";
    }
    reevaluate();
}

void AutomaticHeadTrackingService::handleMotionStatus(
    const MotionStatusSnapshot& status)
{
    m_motionStatus = status;
    if (!m_reevaluating)
        reevaluate();
}

void AutomaticHeadTrackingService::handleManualControlChanged(bool active)
{
    m_manualOverride = active;
    m_targetExpiryTimer.stop();
    m_snapshot.active = false;
    if (active) {
        updateState(AutomaticHeadTrackingState::ManualOverride,
                    QStringLiteral("MANUAL OVERRIDE"));
        qInfo().noquote() << "AUTO_HEAD manual override";
    } else {
        qInfo().noquote() << "AUTO_HEAD resume after manual";
        reevaluate();
    }
}

void AutomaticHeadTrackingService::handleAutomaticControlChanged(
    bool active, const QString& reason)
{
    m_snapshot.active = active;
    if (!active && m_snapshot.enabled && !m_manualOverride
        && !m_videoCallActive) {
        updateState(AutomaticHeadTrackingState::WaitingForMotion, reason);
    }
}

void AutomaticHeadTrackingService::reevaluate()
{
    if (m_reevaluating)
        return;
    m_reevaluating = true;
    if (!m_snapshot.enabled) {
        updateState(AutomaticHeadTrackingState::Disabled,
                    QStringLiteral("自动跟随头部已关闭"));
    } else if (m_manualOverride || m_motionStatus.remoteControlActive) {
        updateState(AutomaticHeadTrackingState::ManualOverride,
                    QStringLiteral("MANUAL OVERRIDE"));
    } else if (m_videoCallActive) {
        updateState(AutomaticHeadTrackingState::VideoCallSuspended,
                    QStringLiteral("视频通话期间暂停"));
    } else if (!m_visionAvailable || m_visionPaused) {
        updateState(AutomaticHeadTrackingState::WaitingForVision,
                    QStringLiteral("等待 Vision 恢复"));
    } else if (m_motionStatus.fault) {
        updateState(AutomaticHeadTrackingState::Fault,
                    QStringLiteral("Motion MCU fault"));
    } else if (!m_motionStatus.uartAvailable || !m_motionStatus.mcuOnline) {
        updateState(AutomaticHeadTrackingState::WaitingForMotion,
                    QStringLiteral("等待 Motion MCU"));
    } else {
        QString error;
        if (!m_motionService->isAutomaticHeadControlActive()
            && !m_motionService->beginAutomaticHeadControl(&error)) {
            updateState(AutomaticHeadTrackingState::WaitingForMotion,
                        error.isEmpty()
                            ? QStringLiteral("无法进入 HEAD_ONLY") : error);
        } else {
            m_snapshot.active = true;
            if (m_snapshot.state != AutomaticHeadTrackingState::Tracking) {
                updateState(AutomaticHeadTrackingState::Searching,
                            QStringLiteral("HEAD_ONLY，等待新目标"));
            }
        }
    }
    m_reevaluating = false;
}

void AutomaticHeadTrackingService::publishLost(
    AutomaticHeadTrackingState state, const QString& detail)
{
    m_targetExpiryTimer.stop();
    if (m_snapshot.active && !m_lostPublished && m_motionService) {
        QString error;
        if (!m_motionService->sendAutomaticTarget(MotionTargetFrame {}, &error)
            && !error.isEmpty()) {
            qWarning().noquote() << "AUTO_HEAD target lost send failed:"
                                 << error;
        } else {
            qInfo().noquote() << "AUTO_HEAD target lost";
        }
    }
    m_lostPublished = true;
    m_snapshot.dx = 0;
    m_snapshot.dy = 0;
    m_snapshot.area = 0;
    updateState(state, detail);
}

void AutomaticHeadTrackingService::updateState(
    AutomaticHeadTrackingState state, const QString& detail)
{
    const bool changed = m_snapshot.state != state
        || m_snapshot.detail != detail;
    m_snapshot.state = state;
    m_snapshot.detail = detail;
    if (changed)
        publishSnapshot();
}

void AutomaticHeadTrackingService::publishSnapshot()
{
    m_snapshot.updatedAt = QDateTime::currentDateTimeUtc();
    emit snapshotChanged(m_snapshot);
}

qint64 AutomaticHeadTrackingService::currentAgeMs(
    const TargetObservation& observation) const
{
    qint64 age = std::max<qint64>(0, observation.ageMs);
    if (observation.timestamp.isValid()) {
        age = std::max(age, std::max<qint64>(
            0, observation.timestamp.msecsTo(QDateTime::currentDateTimeUtc())));
    }
    return age;
}
