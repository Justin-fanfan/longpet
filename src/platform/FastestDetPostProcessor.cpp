#include "FastestDetPostProcessor.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
float sigmoid(float value)
{
    if (value >= 0.0F) {
        const float factor = std::exp(-value);
        return 1.0F / (1.0F + factor);
    }
    const float factor = std::exp(value);
    return factor / (1.0F + factor);
}

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
}

QList<PersonDetection> FastestDetPostProcessor::decode(
    const float* output, int channels, int featureHeight, int featureWidth,
    const QSize& sourceSize, float confidenceThreshold, float nmsThreshold,
    int personClassIndex)
{
    QList<PersonDetection> candidates;
    if (!output || channels <= 5 || featureHeight <= 0 || featureWidth <= 0
        || !sourceSize.isValid() || personClassIndex < 0
        || personClassIndex >= channels - 5) {
        return candidates;
    }

    const int plane = featureHeight * featureWidth;
    for (int y = 0; y < featureHeight; ++y) {
        for (int x = 0; x < featureWidth; ++x) {
            const int offset = y * featureWidth + x;
            const float objectness = output[offset];
            int bestClass = 0;
            float bestClassScore = output[5 * plane + offset];
            for (int classIndex = 1; classIndex < channels - 5; ++classIndex) {
                const float classScore = output[(5 + classIndex) * plane + offset];
                if (classScore > bestClassScore) {
                    bestClass = classIndex;
                    bestClassScore = classScore;
                }
            }
            if (bestClass != personClassIndex)
                continue;

            const float score = std::pow(std::max(objectness, 0.0F), 0.6F)
                * std::pow(std::max(bestClassScore, 0.0F), 0.4F);
            if (score <= confidenceThreshold)
                continue;

            const float centerX = (static_cast<float>(x)
                + std::tanh(output[plane + offset])) / featureWidth;
            const float centerY = (static_cast<float>(y)
                + std::tanh(output[2 * plane + offset])) / featureHeight;
            const float width = sigmoid(output[3 * plane + offset]);
            const float height = sigmoid(output[4 * plane + offset]);
            const QRectF normalized(centerX - width * 0.5F,
                                    centerY - height * 0.5F,
                                    width, height);
            PersonDetection detection = VisionGeometry::personFromNormalizedRect(
                score, normalized, sourceSize);
            if (!detection.boundingBox.isEmpty())
                candidates.append(detection);
        }
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const PersonDetection& first, const PersonDetection& second) {
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
