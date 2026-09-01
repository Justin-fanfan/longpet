#pragma once

#include "model/VisionModels.h"

#include <QList>
#include <QSize>

struct TinyissimoLetterboxTransform {
    QSize sourceSize;
    QSize inputSize;
    float scale = 1.0F;
    float padX = 0.0F;
    float padY = 0.0F;
};

class TinyissimoYoloPostProcessor final {
public:
    static QList<PersonDetection> decode(
        const float* output, int candidateCount, bool channelFirst,
        const TinyissimoLetterboxTransform& transform,
        float confidenceThreshold, float nmsThreshold);
};
