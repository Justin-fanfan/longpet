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
};

namespace HeadTrackingGeometry {
bool targetFromObservation(const TargetObservation& observation,
                           MotionTargetFrame* target,
                           QString* error = nullptr);
bool isControllableStatus(TargetTrackingStatus status);
}

class AutomaticHeadTrackingService final : public QObject {
    Q_OBJECT

public:
    explicit AutomaticHeadTrackingService(
        MotionService* motionService,
        AutomaticHeadTrackingConfiguration configuration = {},
        QObject* parent = nullptr);

    AutomaticHeadTrackingSnapshot snapshot() const;
    bool setEnabled(bool enabled, QString* error = nullptr);

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
    void handleAutomaticControlChanged(bool active, const QString& reason);
    void reevaluate();
    void publishLost(AutomaticHeadTrackingState state,
                     const QString& detail);
    void updateState(AutomaticHeadTrackingState state,
                     const QString& detail);
    void publishSnapshot();
    qint64 currentAgeMs(const TargetObservation& observation) const;

    MotionService* m_motionService = nullptr;
    AutomaticHeadTrackingConfiguration m_configuration;
    AutomaticHeadTrackingSnapshot m_snapshot;
    MotionStatusSnapshot m_motionStatus;
    QTimer m_targetExpiryTimer;
    QElapsedTimer m_targetLogTimer;
    bool m_visionAvailable = false;
    bool m_visionPaused = false;
    bool m_videoCallActive = false;
    bool m_manualOverride = false;
    bool m_lostPublished = true;
    bool m_reevaluating = false;
    bool m_hasConsumedFrame = false;
    quint64 m_lastConsumedFrameSequence = 0;
};
