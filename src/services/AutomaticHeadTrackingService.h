#pragma once

#include "model/AutomaticHeadTrackingModels.h"
#include "model/MotionModels.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

class MotionService;

struct AutomaticHeadTrackingConfiguration {
    int maximumTargetAgeMs = 500;
    int targetExpiryMs = 500;
    int targetLogIntervalMs = 2'000;

    // Conservative V2.3 person-follow defaults. These are deployment
    // calibration values, not universal distance units.
    int targetStableMs = 600;
    int motionStatusMaximumAgeMs = 750;
    int headAlignEnterUs = 220;
    int headAlignExitUs = 100;
    int alignEnterDwellMs = 400;
    int alignExitDwellMs = 400;
    int minimumMotionDurationMs = 300;
    qreal farEnterBboxHeight = 0.28;
    qreal farExitBboxHeight = 0.34;
    qreal nearEnterBboxHeight = 0.78;
    qreal nearExitBboxHeight = 0.70;
    int followForwardSpeed = 12;
    int followRotateSpeed = 10;
};

namespace HeadTrackingGeometry {
bool targetFromObservation(const TargetObservation& observation,
                           MotionTargetFrame* target,
                           QString* error = nullptr);
bool normalizedTargetSize(const TargetObservation& observation,
                          QSizeF* size, QString* error = nullptr);
bool isControllableStatus(TargetTrackingStatus status);
}

// The V2.2 name is retained for source/API compatibility. It now owns both
// HEAD_ONLY and the V2.3 PERSON_FOLLOW high-level policy. MCU code remains a
// low-level executor and watchdog; it never decides distance or alignment.
class AutomaticHeadTrackingService final : public QObject {
    Q_OBJECT

public:
    explicit AutomaticHeadTrackingService(
        MotionService* motionService,
        AutomaticHeadTrackingConfiguration configuration = {},
        QObject* parent = nullptr);

    AutomaticHeadTrackingSnapshot snapshot() const;
    bool setEnabled(bool enabled, QString* error = nullptr);
    bool setMode(AutomaticTrackingMode mode, QString* error = nullptr);

public slots:
    void handleTargetObservation(const TargetObservation& observation);
    void setVisionAvailable(bool available, const QString& detail = {});
    void setVisionPaused(bool paused);
    void setVideoCallActive(bool active);

signals:
    void snapshotChanged(const AutomaticHeadTrackingSnapshot& snapshot);

private:
    void handleMotionStatus(const MotionStatusSnapshot& status);
    void handleManualControlChanged(bool active);
    void handleAutomaticControlChanged(MotionControlMode mode, bool active,
                                       const QString& reason);
    void reevaluate();
    void handleHeadOnlyTarget(const TargetObservation& observation,
                              const MotionTargetFrame& target);
    void handlePersonFollowTarget(const TargetObservation& observation,
                                  const MotionTargetFrame& target);
    void updateDistanceClass(qreal normalizedHeight);
    bool updateAlignmentDecision(int headOffsetUs);
    bool issueFollowMotion(ChassisMotion motion, int speed,
                           const QString& reason);
    void stopFollowMotion(const QString& reason, bool force = false);
    void disablePersonFollow(const QString& reason,
                             AutomaticHeadTrackingState state);
    void resetTargetEvidence();
    void publishLost(AutomaticHeadTrackingState state,
                     const QString& detail);
    void updateState(AutomaticHeadTrackingState state,
                     const QString& detail);
    void updateFollowState(PersonFollowState state, const QString& detail);
    void publishSnapshot();
    qint64 currentAgeMs(const TargetObservation& observation) const;

    MotionService* m_motionService = nullptr;
    AutomaticHeadTrackingConfiguration m_configuration;
    AutomaticHeadTrackingSnapshot m_snapshot;
    MotionStatusSnapshot m_motionStatus;
    QTimer m_targetExpiryTimer;
    QElapsedTimer m_targetLogTimer;
    QElapsedTimer m_targetStableTimer;
    QElapsedTimer m_alignmentCandidateTimer;
    QElapsedTimer m_alignmentExitTimer;
    QElapsedTimer m_motionDurationTimer;
    bool m_visionAvailable = false;
    bool m_visionPaused = false;
    bool m_videoCallActive = false;
    bool m_manualOverride = false;
    bool m_lostPublished = true;
    bool m_reevaluating = false;
    bool m_hasConsumedFrame = false;
    bool m_alignmentActive = false;
    PhysicalHeadDirection m_alignmentCandidate =
        PhysicalHeadDirection::Unknown;
    quint64 m_lastConsumedFrameSequence = 0;
    ChassisMotion m_lastFollowMotion = ChassisMotion::Stopped;
};
