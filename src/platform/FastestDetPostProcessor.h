#pragma once

#include "model/VisionModels.h"

#include <QList>
#include <QSize>

class FastestDetPostProcessor final {
public:
    static QList<PersonDetection> decode(const float* output,
                                         int channels,
                                         int featureHeight,
                                         int featureWidth,
                                         const QSize& sourceSize,
                                         float confidenceThreshold,
                                         float nmsThreshold,
                                         int personClassIndex = 0);
};
