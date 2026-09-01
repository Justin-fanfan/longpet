#include "TinyissimoYoloPostProcessor.h"

#include <algorithm>
#include <cmath>

namespace {
qreal intersectionOverUnion(const QRectF& first, const QRectF& second)
{
    const QRectF intersection = first.intersected(second);
    if (intersection.isEmpty())
        return 0.0;
    const qreal unionArea = first.width() * first.height()
        + second.width() * second.height()
        - intersection.width() * intersection.height();
    return unionArea > 0.0
        ? intersection.width() * intersection.height() / unionArea : 0.0;
}

float outputValue(const float* output, int candidateCount,
                  bool channelFirst, int candidate, int channel)
{
    return channelFirst ? output[channel * candidateCount + candidate]
                        : output[candidate * 5 + channel];
}
}

QList<PersonDetection> TinyissimoYoloPostProcessor::decode(
    const float* output, int candidateCount, bool channelFirst,
    const TinyissimoLetterboxTransform& transform,
    float confidenceThreshold, float nmsThreshold)
{
    QList<PersonDetection> candidates;
    if (!output || candidateCount <= 0 || !transform.sourceSize.isValid()
        || !transform.inputSize.isValid() || transform.scale <= 0.0F) {
        return candidates;
    }

    const float sourceWidth = transform.sourceSize.width();
    const float sourceHeight = transform.sourceSize.height();
    for (int index = 0; index < candidateCount; ++index) {
        const float confidence = outputValue(
            output, candidateCount, channelFirst, index, 4);
        if (!std::isfinite(confidence) || confidence < confidenceThreshold)
            continue;

        const float centerX = outputValue(
            output, candidateCount, channelFirst, index, 0);
        const float centerY = outputValue(
            output, candidateCount, channelFirst, index, 1);
        const float width = outputValue(
            output, candidateCount, channelFirst, index, 2);
        const float height = outputValue(
            output, candidateCount, channelFirst, index, 3);
        if (!std::isfinite(centerX) || !std::isfinite(centerY)
            || !std::isfinite(width) || !std::isfinite(height)
            || width <= 0.0F || height <= 0.0F)
            continue;

        const float left = (centerX - width * 0.5F - transform.padX)
            / transform.scale;
        const float top = (centerY - height * 0.5F - transform.padY)
            / transform.scale;
        const float right = (centerX + width * 0.5F - transform.padX)
            / transform.scale;
        const float bottom = (centerY + height * 0.5F - transform.padY)
            / transform.scale;
        const QRectF normalized(left / sourceWidth, top / sourceHeight,
                                (right - left) / sourceWidth,
                                (bottom - top) / sourceHeight);
        PersonDetection detection = VisionGeometry::personFromNormalizedRect(
            confidence, normalized, transform.sourceSize);
        if (!detection.boundingBox.isEmpty())
            candidates.append(detection);
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const PersonDetection& first,
                 const PersonDetection& second) {
        return first.confidence > second.confidence;
    });
    QList<PersonDetection> kept;
    for (const PersonDetection& candidate : candidates) {
        const bool overlaps = std::any_of(
            kept.cbegin(), kept.cend(), [&](const PersonDetection& accepted) {
                return intersectionOverUnion(candidate.boundingBox,
                                             accepted.boundingBox)
                    > nmsThreshold;
            });
        if (!overlaps)
            kept.append(candidate);
    }
    return kept;
}
