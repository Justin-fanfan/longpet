#pragma once

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>

struct PersonDetection {
    float confidence = 0.0F;
    QRectF boundingBox;
    QPointF normalizedCenter;
    QSizeF normalizedSize;
};

struct VisionFrameResult {
    quint64 frameSequence = 0;
    QDateTime timestamp;
    QSize sourceSize;
    QList<PersonDetection> persons;
    double decodeMs = 0.0;
    double preprocessMs = 0.0;
    double inferenceMs = 0.0;
    double postprocessMs = 0.0;
    double totalMs = 0.0;
};

struct VisionDetectorInfo {
    QString detectorName;
    QString modelPath;
    QString provider;
    QString runtimeVersion;
    QList<qint64> inputShape;
    QList<qint64> outputShape;
    qint64 modelBytes = 0;
    qint64 parameterCount = 0;
    int inferenceThreads = 1;
    float confidenceThreshold = 0.65F;
    float nmsThreshold = 0.45F;
};

enum class TargetTrackingStatus {
    Searching,
    Detected,
    Tracking,
    Corrected,
    Lost,
    Reacquired
};

struct VisionTrackerInfo {
    QString trackerName;
    double processingScale = 1.0;
    int maximumPoints = 0;
    int minimumPoints = 0;
};

struct VisionTrackerResult {
    bool success = false;
    quint64 frameSequence = 0;
    QDateTime timestamp;
    QSize sourceSize;
    PersonDetection target;
    float confidence = 0.0F;
    int trackedPointCount = 0;
    double decodeMs = 0.0;
    double trackingMs = 0.0;
    double totalMs = 0.0;
};

// Stable service-level contract for future tracking, motor control and fall
// detection consumers. An absent target has present=false and an empty box.
struct TargetObservation {
    bool present = false;
    bool fresh = false;
    TargetTrackingStatus status = TargetTrackingStatus::Searching;
    quint64 frameSequence = 0;
    QDateTime timestamp;
    QDateTime publishedAt;
    qint64 ageMs = 0;
    QSize sourceSize;
    PersonDetection target;
    float detectorConfidence = 0.0F;
    float trackerConfidence = 0.0F;
    QString trackerName;
    int trackedPointCount = 0;
    bool detectorRan = false;
    double detectorMs = 0.0;
    double trackerMs = 0.0;
    QString diagnostic;
};

QString targetTrackingStatusName(TargetTrackingStatus status);

namespace VisionGeometry {
PersonDetection personFromNormalizedRect(float confidence,
                                         const QRectF& normalizedRect,
                                         const QSize& sourceSize);
}

Q_DECLARE_METATYPE(PersonDetection)
Q_DECLARE_METATYPE(VisionFrameResult)
Q_DECLARE_METATYPE(VisionDetectorInfo)
Q_DECLARE_METATYPE(TargetTrackingStatus)
Q_DECLARE_METATYPE(VisionTrackerInfo)
Q_DECLARE_METATYPE(VisionTrackerResult)
Q_DECLARE_METATYPE(TargetObservation)
