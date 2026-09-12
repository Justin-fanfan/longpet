#include "AutomaticHeadTrackingService.h"

#include "services/MotionService.h"

#include <QDebug>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
qreal clampUnit(qreal value)
{
    return std::clamp(value, 0.0, 1.0);
}

PhysicalHeadDirection strongHeadDirection(int offsetUs, int thresholdUs)
{
    if (offsetUs <= -thresholdUs)
        return PhysicalHeadDirection::Left;
    if (offsetUs >= thresholdUs)
        return PhysicalHeadDirection::Right;
    return PhysicalHeadDirection::Centered;
}
}

bool HeadTrackingGeometry::isControllableStatus(TargetTrackingStatus status)
{
    return status == TargetTrackingStatus::Detected
        || status == TargetTrackingStatus::Tracking
        || status == TargetTrackingStatus::Corrected
        || status == TargetTrackingStatus::Reacquired;
}

bool HeadTrackingGeometry::normalizedTargetSize(
    const TargetObservation& observation, QSizeF* size, QString* error)
{
    if (!size || !observation.sourceSize.isValid()
        || observation.sourceSize.width() <= 0
        || observation.sourceSize.height() <= 0) {
        if (error)
            *error = QStringLiteral("视觉源尺寸无效");
        return false;
    }
    qreal width = observation.target.normalizedSize.width();
    qreal height = observation.target.normalizedSize.height();
    if (!std::isfinite(width) || !std::isfinite(height)
        || width <= 0.0 || height <= 0.0) {
        const QRectF clipped = observation.target.boundingBox.intersected(
            QRectF(QPointF(0.0, 0.0), QSizeF(observation.sourceSize)));
        if (!clipped.isValid() || clipped.isEmpty()) {
            if (error)
                *error = QStringLiteral("人物 bbox 无效");
            return false;
        }
        width = clipped.width() / observation.sourceSize.width();
        height = clipped.height() / observation.sourceSize.height();
    }
    *size = QSizeF(clampUnit(width), clampUnit(height));
    return size->width() > 0.0 && size->height() > 0.0;
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
    QSizeF normalizedSize;
    QString sizeError;
    const bool normalizedSizeValid = normalizedTargetSize(
        observation, &normalizedSize, &sizeError);
    const bool normalizedCenterValid = std::isfinite(centerX)
        && std::isfinite(centerY);
    qreal width = 0.0;
    qreal height = 0.0;
    if (normalizedCenterValid && normalizedSizeValid) {
        centerX = clampUnit(centerX) * sourceWidth;
        centerY = clampUnit(centerY) * sourceHeight;
        width = normalizedSize.width() * sourceWidth;
        height = normalizedSize.height() * sourceHeight;
    } else {
        const QRectF clipped = observation.target.boundingBox.intersected(
            QRectF(0.0, 0.0, sourceWidth, sourceHeight));
        if (!clipped.isValid() || clipped.isEmpty()) {
            if (error)
                *error = sizeError.isEmpty()
                    ? QStringLiteral("人物 bbox 无效") : sizeError;
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
    qRegisterMetaType<AutomaticTrackingMode>();
    m_configuration.maximumTargetAgeMs = std::clamp(
        m_configuration.maximumTargetAgeMs, 100, 2'500);
    m_configuration.targetExpiryMs = std::clamp(
        m_configuration.targetExpiryMs, 200, 2'500);
    m_configuration.targetLogIntervalMs = std::max(
        250, m_configuration.targetLogIntervalMs);
    m_configuration.targetStableMs = std::clamp(
        m_configuration.targetStableMs, 0, 5'000);
    m_configuration.motionStatusMaximumAgeMs = std::clamp(
        m_configuration.motionStatusMaximumAgeMs, 200, 5'000);
    m_configuration.headAlignEnterUs = std::clamp(
        m_configuration.headAlignEnterUs, 20, 700);
    m_configuration.headAlignExitUs = std::clamp(
        m_configuration.headAlignExitUs, 0,
        m_configuration.headAlignEnterUs - 1);
    m_configuration.alignEnterDwellMs = std::clamp(
        m_configuration.alignEnterDwellMs, 0, 5'000);
    m_configuration.alignExitDwellMs = std::clamp(
        m_configuration.alignExitDwellMs, 0, 5'000);
    m_configuration.minimumMotionDurationMs = std::clamp(
        m_configuration.minimumMotionDurationMs, 0, 5'000);
    m_configuration.farEnterBboxHeight = std::clamp(
        m_configuration.farEnterBboxHeight, 0.02, 0.90);
    m_configuration.farExitBboxHeight = std::clamp(
        m_configuration.farExitBboxHeight,
        m_configuration.farEnterBboxHeight + 0.01, 0.94);
    m_configuration.nearExitBboxHeight = std::clamp(
        m_configuration.nearExitBboxHeight,
        m_configuration.farExitBboxHeight + 0.01, 0.97);
    m_configuration.nearEnterBboxHeight = std::clamp(
        m_configuration.nearEnterBboxHeight,
        m_configuration.nearExitBboxHeight + 0.01, 0.99);
    m_configuration.followForwardSpeed = std::clamp(
        m_configuration.followForwardSpeed, 1, 100);
    m_configuration.followRotateSpeed = std::clamp(
        m_configuration.followRotateSpeed, 1, 100);

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
        connect(m_motionService, &MotionService::automaticControlChanged,
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
    return setMode(enabled ? AutomaticTrackingMode::HeadOnly
                           : AutomaticTrackingMode::Disabled, error);
}

bool AutomaticHeadTrackingService::setMode(AutomaticTrackingMode mode,
                                           QString* error)
{
    if (!m_motionService) {
        if (error)
            *error = QStringLiteral("MotionService 未配置");
        return false;
    }
    if (m_snapshot.mode == mode)
        return true;
    if (mode == AutomaticTrackingMode::PersonFollow) {
        if (!m_visionAvailable || m_visionPaused || m_videoCallActive) {
            if (error)
                *error = QStringLiteral("Vision 当前不可用于人物跟随");
            return false;
        }
        if (!m_motionStatus.uartAvailable || !m_motionStatus.mcuOnline
            || m_motionStatus.fault) {
            if (error)
                *error = QStringLiteral("Motion MCU 当前不可用于人物跟随");
            return false;
        }
        if (!m_motionStatus.headOffsetAvailable) {
            if (error) {
                *error = QStringLiteral(
                    "Motion MCU 未报告物理头部偏移，请先更新 V2.3 固件");
            }
            return false;
        }
    }

    m_reevaluating = true;
    if (m_snapshot.mode == AutomaticTrackingMode::PersonFollow
        && m_snapshot.active) {
        stopFollowMotion(QStringLiteral("切换自动跟踪模式"), true);
    }
    m_snapshot.mode = mode;
    m_snapshot.enabled = mode != AutomaticTrackingMode::Disabled;
    m_motionService->endAutomaticHeadControl(
        QStringLiteral("切换自动跟踪模式"));
    resetTargetEvidence();
    m_snapshot.active = false;
    m_snapshot.followState = mode == AutomaticTrackingMode::PersonFollow
        ? PersonFollowState::Acquiring : PersonFollowState::Disabled;
    m_reevaluating = false;

    if (mode == AutomaticTrackingMode::Disabled) {
        updateState(AutomaticHeadTrackingState::Disabled,
                    QStringLiteral("自动视觉运动已关闭"));
        qInfo().noquote() << "AUTO_TRACK disabled";
        return true;
    }
    qInfo().noquote() << "AUTO_TRACK requested"
                      << automaticTrackingModeName(mode);
    if (mode == AutomaticTrackingMode::PersonFollow) {
        qInfo().noquote()
            << "AUTO_FOLLOW config"
            << "align_enter_us=" << m_configuration.headAlignEnterUs
            << "align_exit_us=" << m_configuration.headAlignExitUs
            << "far_enter_h=" << m_configuration.farEnterBboxHeight
            << "far_exit_h=" << m_configuration.farExitBboxHeight
            << "near_enter_h=" << m_configuration.nearEnterBboxHeight
            << "near_exit_h=" << m_configuration.nearExitBboxHeight
            << "forward_speed=" << m_configuration.followForwardSpeed
            << "rotate_speed=" << m_configuration.followRotateSpeed;
    }
    reevaluate();
    return m_snapshot.active
        || m_snapshot.state == AutomaticHeadTrackingState::WaitingForVision
        || m_snapshot.state == AutomaticHeadTrackingState::WaitingForMotion;
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
        qInfo().noquote() << "AUTO_TRACK stale target dropped frame="
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
    if (m_snapshot.mode == AutomaticTrackingMode::PersonFollow)
        handlePersonFollowTarget(observation, target);
    else
        handleHeadOnlyTarget(observation, target);

    if (!m_targetLogTimer.isValid()
        || m_targetLogTimer.elapsed() >= m_configuration.targetLogIntervalMs) {
        qInfo().noquote() << "AUTO_TRACK target"
                          << "mode="
                          << automaticTrackingModeName(m_snapshot.mode)
                          << "dx=" << target.dx << "dy=" << target.dy
                          << "bbox_h=" << m_snapshot.normalizedBboxHeight
                          << "head_offset_us=" << m_snapshot.headOffsetUs
                          << "follow_state="
                          << personFollowStateName(m_snapshot.followState)
                          << "chassis="
                          << chassisMotionName(m_snapshot.chassisMotion)
                          << "age_ms=" << m_snapshot.targetAgeMs
                          << "frame=" << observation.frameSequence;
        if (m_targetLogTimer.isValid())
            m_targetLogTimer.restart();
        else
            m_targetLogTimer.start();
    }
}

void AutomaticHeadTrackingService::handleHeadOnlyTarget(
    const TargetObservation&, const MotionTargetFrame& target)
{
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
    const bool changed = m_snapshot.state
            != AutomaticHeadTrackingState::Tracking
        || m_snapshot.detail != QStringLiteral("正在自动跟随头部");
    updateState(AutomaticHeadTrackingState::Tracking,
                QStringLiteral("正在自动跟随头部"));
    if (!changed)
        publishSnapshot();
}

void AutomaticHeadTrackingService::handlePersonFollowTarget(
    const TargetObservation& observation, const MotionTargetFrame& target)
{
    QSizeF normalizedSize;
    QString geometryError;
    if (!HeadTrackingGeometry::normalizedTargetSize(
            observation, &normalizedSize, &geometryError)) {
        publishLost(AutomaticHeadTrackingState::Searching, geometryError);
        return;
    }
    QString motionError;
    if (!m_motionService->sendAutomaticTarget(target, &motionError)) {
        disablePersonFollow(
            motionError.isEmpty() ? QStringLiteral("TARGET 发送失败")
                                  : motionError,
            AutomaticHeadTrackingState::WaitingForMotion);
        return;
    }

    m_lostPublished = false;
    m_snapshot.dx = target.dx;
    m_snapshot.dy = target.dy;
    m_snapshot.area = target.area;
    m_snapshot.normalizedBboxWidth = normalizedSize.width();
    m_snapshot.normalizedBboxHeight = normalizedSize.height();
    m_snapshot.normalizedBboxAreaRatio =
        normalizedSize.width() * normalizedSize.height();
    m_targetExpiryTimer.start();
    updateDistanceClass(normalizedSize.height());

    if (!m_targetStableTimer.isValid())
        m_targetStableTimer.start();
    m_snapshot.targetStableMs = m_targetStableTimer.elapsed();
    if (m_snapshot.targetStableMs < m_configuration.targetStableMs) {
        stopFollowMotion(QStringLiteral("等待目标持续稳定"));
        updateFollowState(PersonFollowState::Acquiring,
                          QStringLiteral("确认同一人物目标"));
        return;
    }

    const qint64 motionStatusAge = m_motionStatus.mcuReportedAt.isValid()
        ? std::max<qint64>(0, m_motionStatus.mcuReportedAt.msecsTo(
              QDateTime::currentDateTimeUtc()))
        : std::numeric_limits<qint64>::max();
    if (!m_motionStatus.headOffsetAvailable
        || motionStatusAge > m_configuration.motionStatusMaximumAgeMs) {
        stopFollowMotion(QStringLiteral("等待新鲜头部位置"), true);
        updateFollowState(PersonFollowState::Holding,
                          QStringLiteral("等待 Motion STATUS"));
        return;
    }

    m_snapshot.headOffsetAvailable = true;
    m_snapshot.headOffsetUs = m_motionStatus.headOffsetUs;
    m_snapshot.headDirection = physicalHeadDirection(
        m_motionStatus.headOffsetUs, m_configuration.headAlignExitUs);
    const bool aligning = updateAlignmentDecision(
        m_motionStatus.headOffsetUs);
    const bool alignmentPending = m_alignmentCandidateTimer.isValid()
        || m_alignmentExitTimer.isValid();
    if (m_alignmentExitTimer.isValid()) {
        stopFollowMotion(QStringLiteral("头部偏移消抖"));
        updateFollowState(PersonFollowState::Aligning,
                          QStringLiteral("确认头部已稳定回中"));
        return;
    }
    if (aligning) {
        const ChassisMotion rotation = m_alignmentCandidate
                == PhysicalHeadDirection::Left
            ? ChassisMotion::RotateLeft : ChassisMotion::RotateRight;
        if (!issueFollowMotion(rotation, m_configuration.followRotateSpeed,
                               QStringLiteral("头身对齐"))) {
            return;
        }
        updateFollowState(PersonFollowState::Aligning,
                          QStringLiteral("底盘正在跟随头部方向对齐"));
        return;
    }
    if (alignmentPending) {
        stopFollowMotion(QStringLiteral("头部偏移消抖"));
        updateFollowState(PersonFollowState::Aligning,
                          QStringLiteral("确认头部对齐状态"));
        return;
    }

    if (m_snapshot.distanceClass == PersonDistanceClass::Far) {
        if (!issueFollowMotion(ChassisMotion::Forward,
                               m_configuration.followForwardSpeed,
                               QStringLiteral("人物距离较远"))) {
            return;
        }
        updateFollowState(PersonFollowState::Approaching,
                          QStringLiteral("已对齐，低速接近人物"));
    } else {
        stopFollowMotion(m_snapshot.distanceClass == PersonDistanceClass::Near
                             ? QStringLiteral("人物距离较近")
                             : QStringLiteral("人物距离合适"));
        updateFollowState(PersonFollowState::Holding,
                          m_snapshot.distanceClass == PersonDistanceClass::Near
                              ? QStringLiteral("距离过近，保持停止")
                              : QStringLiteral("距离合适，保持停止"));
    }
}

void AutomaticHeadTrackingService::updateDistanceClass(qreal height)
{
    const PersonDistanceClass previous = m_snapshot.distanceClass;
    switch (m_snapshot.distanceClass) {
    case PersonDistanceClass::Far:
        if (height >= m_configuration.farExitBboxHeight)
            m_snapshot.distanceClass = height >= m_configuration.nearEnterBboxHeight
                ? PersonDistanceClass::Near : PersonDistanceClass::Good;
        break;
    case PersonDistanceClass::Near:
        if (height <= m_configuration.nearExitBboxHeight)
            m_snapshot.distanceClass = height <= m_configuration.farEnterBboxHeight
                ? PersonDistanceClass::Far : PersonDistanceClass::Good;
        break;
    case PersonDistanceClass::Good:
    case PersonDistanceClass::Unknown:
        if (height <= m_configuration.farEnterBboxHeight)
            m_snapshot.distanceClass = PersonDistanceClass::Far;
        else if (height >= m_configuration.nearEnterBboxHeight)
            m_snapshot.distanceClass = PersonDistanceClass::Near;
        else
            m_snapshot.distanceClass = PersonDistanceClass::Good;
        break;
    }
    if (previous != m_snapshot.distanceClass) {
        qInfo().noquote() << "AUTO_FOLLOW distance"
                          << personDistanceClassName(previous) << "->"
                          << personDistanceClassName(m_snapshot.distanceClass)
                          << "bbox_height=" << height;
    }
}

bool AutomaticHeadTrackingService::updateAlignmentDecision(int headOffsetUs)
{
    const int magnitude = std::abs(headOffsetUs);
    const PhysicalHeadDirection strong = strongHeadDirection(
        headOffsetUs, m_configuration.headAlignEnterUs);
    const PhysicalHeadDirection weak = physicalHeadDirection(
        headOffsetUs, m_configuration.headAlignExitUs);

    if (m_alignmentActive) {
        if (magnitude <= m_configuration.headAlignExitUs) {
            if (!m_alignmentExitTimer.isValid())
                m_alignmentExitTimer.start();
            const qint64 required = std::max<qint64>(
                m_configuration.alignExitDwellMs,
                static_cast<qint64>(m_configuration.minimumMotionDurationMs)
                    - (m_motionDurationTimer.isValid()
                           ? m_motionDurationTimer.elapsed() : 0));
            // Stop while confirming center so the dwell cannot create
            // overshoot; distance motion remains blocked until it expires.
            stopFollowMotion(QStringLiteral("头部已回中，确认稳定"));
            if (m_alignmentExitTimer.elapsed() >= std::max<qint64>(0, required)) {
                m_alignmentActive = false;
                m_alignmentCandidate = PhysicalHeadDirection::Unknown;
                m_alignmentExitTimer.invalidate();
                m_alignmentCandidateTimer.invalidate();
            }
            return m_alignmentActive;
        }
        m_alignmentExitTimer.invalidate();
        if (weak != PhysicalHeadDirection::Centered
            && weak != m_alignmentCandidate) {
            // Never reverse the chassis in one observation. Stop first and
            // require a fresh dwell in the opposite direction.
            stopFollowMotion(QStringLiteral("头部方向反转，重新消抖"), true);
            m_alignmentActive = false;
            m_alignmentCandidate = weak;
            m_alignmentCandidateTimer.restart();
            return false;
        }
        return true;
    }

    if (strong == PhysicalHeadDirection::Centered) {
        m_alignmentCandidate = PhysicalHeadDirection::Unknown;
        m_alignmentCandidateTimer.invalidate();
        return false;
    }
    if (m_alignmentCandidate != strong
        || !m_alignmentCandidateTimer.isValid()) {
        m_alignmentCandidate = strong;
        m_alignmentCandidateTimer.restart();
        return false;
    }
    if (m_alignmentCandidateTimer.elapsed()
        < m_configuration.alignEnterDwellMs) {
        return false;
    }
    m_alignmentActive = true;
    m_alignmentCandidateTimer.invalidate();
    m_motionDurationTimer.restart();
    return true;
}

bool AutomaticHeadTrackingService::issueFollowMotion(
    ChassisMotion motion, int speed, const QString& reason)
{
    QString error;
    if (!m_motionService->sendAutomaticFollowMove(motion, speed, &error)) {
        disablePersonFollow(
            error.isEmpty() ? QStringLiteral("FOLLOW_MOVE 发送失败") : error,
            AutomaticHeadTrackingState::WaitingForMotion);
        return false;
    }
    if (motion != m_lastFollowMotion)
        m_motionDurationTimer.restart();
    m_lastFollowMotion = motion;
    m_snapshot.chassisMotion = motion;
    m_snapshot.detail = reason;
    return true;
}

void AutomaticHeadTrackingService::stopFollowMotion(const QString& reason,
                                                     bool force)
{
    if (!m_motionService || !m_snapshot.active
        || m_snapshot.mode != AutomaticTrackingMode::PersonFollow) {
        m_lastFollowMotion = ChassisMotion::Stopped;
        m_snapshot.chassisMotion = ChassisMotion::Stopped;
        return;
    }
    if (!force && m_lastFollowMotion == ChassisMotion::Stopped) {
        m_snapshot.chassisMotion = ChassisMotion::Stopped;
        return;
    }
    QString error;
    if (!m_motionService->sendAutomaticFollowMove(
            ChassisMotion::Stopped, 0, &error) && !error.isEmpty()) {
        qWarning().noquote() << "AUTO_FOLLOW stop failed:" << error;
    }
    m_lastFollowMotion = ChassisMotion::Stopped;
    m_snapshot.chassisMotion = ChassisMotion::Stopped;
    m_snapshot.detail = reason;
}

void AutomaticHeadTrackingService::disablePersonFollow(
    const QString& reason, AutomaticHeadTrackingState state)
{
    if (m_snapshot.mode != AutomaticTrackingMode::PersonFollow)
        return;
    stopFollowMotion(reason, true);
    m_snapshot.mode = AutomaticTrackingMode::Disabled;
    m_snapshot.enabled = false;
    if (m_motionService)
        m_motionService->endAutomaticHeadControl(reason);
    m_snapshot.active = false;
    resetTargetEvidence();
    m_snapshot.followState = PersonFollowState::Disabled;
    updateState(state, reason);
    qWarning().noquote() << "AUTO_FOLLOW disabled:" << reason;
}

void AutomaticHeadTrackingService::setVisionAvailable(
    bool available, const QString& detail)
{
    m_visionAvailable = available;
    if (!available && m_motionService) {
        m_motionService->endAutomaticHeadControl(
            detail.isEmpty() ? QStringLiteral("Vision unavailable") : detail);
        m_snapshot.active = false;
        resetTargetEvidence();
    }
    reevaluate();
}

void AutomaticHeadTrackingService::setVisionPaused(bool paused)
{
    if (m_visionPaused == paused)
        return;
    m_visionPaused = paused;
    if (paused && m_motionService) {
        m_motionService->endAutomaticHeadControl(
            m_videoCallActive ? QStringLiteral("video-call suspend")
                              : QStringLiteral("Vision paused"));
        m_snapshot.active = false;
        resetTargetEvidence();
    }
    reevaluate();
}

void AutomaticHeadTrackingService::setVideoCallActive(bool active)
{
    if (m_videoCallActive == active)
        return;
    m_videoCallActive = active;
    if (active && m_snapshot.mode == AutomaticTrackingMode::PersonFollow) {
        disablePersonFollow(QStringLiteral("视频通话已关闭人物跟随"),
                            AutomaticHeadTrackingState::VideoCallSuspended);
    } else if (active && m_motionService) {
        m_motionService->endAutomaticHeadControl(
            QStringLiteral("video-call suspend"));
        m_snapshot.active = false;
        resetTargetEvidence();
        updateState(AutomaticHeadTrackingState::VideoCallSuspended,
                    QStringLiteral("视频通话期间暂停"));
    } else {
        reevaluate();
    }
}

void AutomaticHeadTrackingService::handleMotionStatus(
    const MotionStatusSnapshot& status)
{
    m_motionStatus = status;
    m_snapshot.headOffsetAvailable = status.headOffsetAvailable;
    m_snapshot.headOffsetUs = status.headOffsetUs;
    m_snapshot.headDirection = status.headOffsetAvailable
        ? physicalHeadDirection(status.headOffsetUs,
                                m_configuration.headAlignExitUs)
        : PhysicalHeadDirection::Unknown;
    if (m_snapshot.mode == AutomaticTrackingMode::PersonFollow
        && m_snapshot.active
        && (status.fault || !status.uartAvailable || !status.mcuOnline)) {
        disablePersonFollow(
            status.fault ? QStringLiteral("Motion MCU fault，人物跟随已关闭")
                         : QStringLiteral("Motion MCU 断线，人物跟随已关闭"),
            status.fault ? AutomaticHeadTrackingState::Fault
                         : AutomaticHeadTrackingState::WaitingForMotion);
        return;
    }
    if (!m_reevaluating)
        reevaluate();
}

void AutomaticHeadTrackingService::handleManualControlChanged(bool active)
{
    m_manualOverride = active;
    resetTargetEvidence();
    m_snapshot.active = false;
    if (active) {
        if (m_snapshot.mode == AutomaticTrackingMode::PersonFollow) {
            m_snapshot.mode = AutomaticTrackingMode::Disabled;
            m_snapshot.enabled = false;
            m_snapshot.followState = PersonFollowState::Disabled;
        }
        updateState(AutomaticHeadTrackingState::ManualOverride,
                    QStringLiteral("MANUAL OVERRIDE"));
        qInfo().noquote() << "AUTO_TRACK manual override";
    } else {
        // HEAD_ONLY retains its V2.2 resume behavior. PERSON_FOLLOW was
        // explicitly cleared above and must be enabled again by the family.
        reevaluate();
    }
}

void AutomaticHeadTrackingService::handleAutomaticControlChanged(
    MotionControlMode mode, bool active, const QString& reason)
{
    m_snapshot.active = active;
    if (!active && mode == MotionControlMode::Follow
        && m_snapshot.mode == AutomaticTrackingMode::PersonFollow
        && !m_reevaluating && !m_manualOverride && !m_videoCallActive) {
        disablePersonFollow(reason.isEmpty()
                                ? QStringLiteral("FOLLOW 控制权已丢失")
                                : reason,
                            AutomaticHeadTrackingState::WaitingForMotion);
    } else if (!active && m_snapshot.enabled && !m_manualOverride
               && !m_videoCallActive && !m_reevaluating) {
        updateState(AutomaticHeadTrackingState::WaitingForMotion, reason);
    }
}

void AutomaticHeadTrackingService::reevaluate()
{
    if (m_reevaluating)
        return;
    m_reevaluating = true;
    if (m_snapshot.mode == AutomaticTrackingMode::Disabled) {
        updateState(AutomaticHeadTrackingState::Disabled,
                    QStringLiteral("自动视觉运动已关闭"));
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
        const bool follow = m_snapshot.mode
            == AutomaticTrackingMode::PersonFollow;
        const bool alreadyActive = follow
            ? m_motionService->isAutomaticFollowControlActive()
            : m_motionService->isAutomaticHeadControlActive();
        const bool began = alreadyActive || (follow
            ? m_motionService->beginAutomaticFollowControl(&error)
            : m_motionService->beginAutomaticHeadControl(&error));
        if (!began) {
            updateState(AutomaticHeadTrackingState::WaitingForMotion,
                        error.isEmpty()
                            ? QStringLiteral("无法进入自动控制模式") : error);
        } else {
            m_snapshot.active = true;
            if (follow) {
                updateFollowState(PersonFollowState::Acquiring,
                                  QStringLiteral("FOLLOW，等待稳定目标"));
            } else if (m_snapshot.state
                       != AutomaticHeadTrackingState::Tracking) {
                updateState(AutomaticHeadTrackingState::Searching,
                            QStringLiteral("HEAD_ONLY，等待新目标"));
            }
        }
    }
    m_reevaluating = false;
}

void AutomaticHeadTrackingService::resetTargetEvidence()
{
    m_targetExpiryTimer.stop();
    m_targetStableTimer.invalidate();
    m_alignmentCandidateTimer.invalidate();
    m_alignmentExitTimer.invalidate();
    m_motionDurationTimer.invalidate();
    m_hasConsumedFrame = false;
    m_lastConsumedFrameSequence = 0;
    m_lostPublished = true;
    m_alignmentActive = false;
    m_alignmentCandidate = PhysicalHeadDirection::Unknown;
    m_lastFollowMotion = ChassisMotion::Stopped;
    m_snapshot.dx = 0;
    m_snapshot.dy = 0;
    m_snapshot.area = 0;
    m_snapshot.normalizedBboxWidth = 0.0;
    m_snapshot.normalizedBboxHeight = 0.0;
    m_snapshot.normalizedBboxAreaRatio = 0.0;
    m_snapshot.distanceClass = PersonDistanceClass::Unknown;
    m_snapshot.chassisMotion = ChassisMotion::Stopped;
    m_snapshot.targetStableMs = 0;
}

void AutomaticHeadTrackingService::publishLost(
    AutomaticHeadTrackingState state, const QString& detail)
{
    m_targetExpiryTimer.stop();
    if (m_snapshot.active && !m_lostPublished && m_motionService) {
        QString error;
        if (m_snapshot.mode == AutomaticTrackingMode::PersonFollow)
            stopFollowMotion(QStringLiteral("目标丢失"), true);
        if (!m_motionService->sendAutomaticTarget(MotionTargetFrame {}, &error)
            && !error.isEmpty()) {
            qWarning().noquote() << "AUTO_TRACK target lost send failed:"
                                 << error;
        } else {
            qInfo().noquote() << "AUTO_TRACK target lost";
        }
    }
    const bool personFollow = m_snapshot.mode
        == AutomaticTrackingMode::PersonFollow;
    resetTargetEvidence();
    m_lostPublished = true;
    if (personFollow)
        updateFollowState(PersonFollowState::Lost, detail);
    else
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

void AutomaticHeadTrackingService::updateFollowState(
    PersonFollowState state, const QString& detail)
{
    const bool changed = m_snapshot.followState != state
        || m_snapshot.detail != detail;
    m_snapshot.followState = state;
    m_snapshot.state = state == PersonFollowState::Lost
        ? AutomaticHeadTrackingState::Searching
        : AutomaticHeadTrackingState::Tracking;
    m_snapshot.detail = detail;
    if (changed) {
        qInfo().noquote() << "AUTO_FOLLOW state"
                          << personFollowStateName(state)
                          << "head_offset_us=" << m_snapshot.headOffsetUs
                          << "bbox_height=" << m_snapshot.normalizedBboxHeight
                          << "distance="
                          << personDistanceClassName(m_snapshot.distanceClass)
                          << "chassis="
                          << chassisMotionName(m_snapshot.chassisMotion)
                          << "detail=" << detail;
    }
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
