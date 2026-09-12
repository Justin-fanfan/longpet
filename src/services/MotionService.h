#pragma once

#include "model/MotionModels.h"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QObject>
#include <QTimer>

class FamilyMotionControlPort;
class MotionPort;

struct MotionServiceConfiguration {
    int moveRefreshIntervalMs = 150;
    int remoteLeaseTimeoutMs = 350;
    int defaultSpeed = 20;
    int headStepUs = 20;
    int statusPollIntervalMs = 250;
    int mcuOfflineTimeoutMs = 2'500;
};

class MotionService final : public QObject {
    Q_OBJECT

public:
    MotionService(MotionPort* motionPort,
                  FamilyMotionControlPort* controlPort,
                  MotionServiceConfiguration configuration = {},
                  QObject* parent = nullptr);
    ~MotionService() override;

    bool start(QHostAddress address, quint16 controlPort,
               const QString& serialDevice, int baudRate,
               QString* error = nullptr);
    void stop();
    bool isAvailable() const;
    FamilyMotionSession createRemoteSession(QString* error = nullptr);
    MotionStatusSnapshot status() const;
    bool beginAutomaticHeadControl(QString* error = nullptr);
    bool beginAutomaticFollowControl(QString* error = nullptr);
    void endAutomaticHeadControl(const QString& reason = {});
    bool sendAutomaticTarget(const MotionTargetFrame& target,
                             QString* error = nullptr);
    bool sendAutomaticFollowMove(ChassisMotion motion, int speed,
                                 QString* error = nullptr);
    bool isAutomaticHeadControlActive() const;
    bool isAutomaticFollowControlActive() const;
    MotionControlMode automaticControlMode() const;
    bool isManualControlActive() const;

signals:
    void statusChanged(const MotionStatusSnapshot& status);
    void manualControlChanged(bool active);
    void automaticHeadControlChanged(bool active, const QString& reason);
    void automaticControlChanged(MotionControlMode mode, bool active,
                                 const QString& reason);

private:
    void handleControllerStart(const QString& sessionId);
    void handleControllerStopped(const QString& reason);
    void handleChassisCommand(ChassisMotion motion, int speed);
    void handleStopRequested();
    void handleHeadCommand(HeadMotion motion, int stepUs);
    void handleTransportAvailability(bool available, const QString& detail);
    void handleMcuActivity();
    void handleMcuStatus(const MotionStatusSnapshot& status);
    void handleMcuFault(const QString& reason);
    void refreshMotionLease();
    void pollMcuStatus();
    void publishStatus();
    bool commandStop(QString* error = nullptr);
    bool beginAutomaticControl(MotionControlMode mode, QString* error);
    void clearAutomaticControl(const QString& reason, bool sendSafeCommands);
    void clearChassisState(const QString& reason);
    void endRemoteControl(const QString& reason, bool notifyClient,
                          const QString& errorCode = {});
    bool isMcuFresh() const;

    MotionPort* m_motionPort = nullptr;
    FamilyMotionControlPort* m_controlPort = nullptr;
    MotionServiceConfiguration m_configuration;
    MotionStatusSnapshot m_status;
    QElapsedTimer m_clock;
    QTimer m_refreshTimer;
    QTimer m_statusTimer;
    QString m_activeSessionId;
    MotionControlMode m_automaticControlMode = MotionControlMode::Unknown;
    ChassisMotion m_requestedMotion = ChassisMotion::Stopped;
    int m_requestedSpeed = 0;
    qint64 m_lastRemoteRefreshMs = -1;
    qint64 m_lastMoveSentMs = -1;
    qint64 m_lastMcuActivityMs = -1;
    bool m_started = false;
    bool m_stopping = false;
};
