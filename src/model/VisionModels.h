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
    QString modelPath;
    QString provider;
    QString runtimeVersion;
    QList<qint64> inputShape;
    QList<qint64> outputShape;
    int inferenceThreads = 1;
    float confidenceThreshold = 0.65F;
    float nmsThreshold = 0.45F;
};

namespace VisionGeometry {
PersonDetection personFromNormalizedRect(float confidence,
                                         const QRectF& normalizedRect,
                                         const QSize& sourceSize);
}

Q_DECLARE_METATYPE(PersonDetection)
Q_DECLARE_METATYPE(VisionFrameResult)
Q_DECLARE_METATYPE(VisionDetectorInfo)
