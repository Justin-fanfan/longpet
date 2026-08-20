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

private:
    VisionAdapter* m_adapter = nullptr;
    Clock m_clock;
    VisionStatus m_status;
    QHash<int, QDateTime> m_lastAcceptedAt;
};
