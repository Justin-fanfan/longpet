#pragma once

#include "model/VisionModels.h"

#include <QHash>
#include <QObject>

#include <functional>

class VisionAdapter;

class VisionService final : public QObject {
    Q_OBJECT

public:
    using Clock = std::function<QDateTime()>;

    explicit VisionService(VisionAdapter* adapter, Clock clock = {},
                           QObject* parent = nullptr);

    VisionStatus status() const;
    static bool isDetectionValid(const VisionDetection& detection,
                                 QString* reason = nullptr);
    static int defaultCooldownMs(VisionEventType type);
    VisionConfig config() const;
    bool setEnabled(bool enabled);
    bool start();
    void stop();
    bool restart();
    bool reconfigure(const VisionConfig& config, QString* error = nullptr);
    void injectDiagnosticDetection(VisionEventType type,
                                   double confidence = 1.0);

public slots:
    void handleDetection(const VisionDetection& detection);
    void handleRuntimeStatus(const VisionStatus& status);

signals:
    void statusChanged(const VisionStatus& status);
    void detectionAccepted(const VisionDetection& detection);
    void detectionSuppressed(const VisionDetection& detection,
                             const QString& reason);
    void personDetected(const VisionDetection& detection);
    void fallCandidateDetected(const VisionDetection& detection);
    void fallConfirmed(const VisionDetection& detection);
    void waveDetected(const VisionDetection& detection);
    void diagnosticInjectionRequested(const VisionDetection& detection);
    void adapterDiagnostic(const QString& message);
    void recoveryScheduled(int attempt, int delayMs);
    void poseDataAvailable(const PoseData& pose);
private:
    VisionAdapter* m_adapter = nullptr;
    Clock m_clock;
    VisionStatus m_status;
    QHash<int, QDateTime> m_lastAcceptedAt;
};
