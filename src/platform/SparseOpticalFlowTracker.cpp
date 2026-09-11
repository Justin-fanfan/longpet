#include "SparseOpticalFlowTracker.h"
#include "CameraImageTransform.h"

#include <QElapsedTimer>

#include <algorithm>
#include <cmath>
#include <vector>

#ifdef LONGPET_HAS_VISION
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>
#endif

namespace {
double configuredDouble(const char* name, double fallback,
                        double minimum, double maximum)
{
    bool valid = false;
    const double value = qEnvironmentVariable(name).toDouble(&valid);
    return valid && value >= minimum && value <= maximum ? value : fallback;
}

int configuredInteger(const char* name, int fallback,
                      int minimum, int maximum)
{
    bool valid = false;
    const int value = qEnvironmentVariableIntValue(name, &valid);
    return valid && value >= minimum && value <= maximum ? value : fallback;
}

#ifdef LONGPET_HAS_VISION
float median(std::vector<float> values)
{
    if (values.empty())
        return 0.0F;
    const auto middle = values.begin()
        + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    float result = *middle;
    if ((values.size() % 2) == 0) {
        const auto lower = std::max_element(values.begin(), middle);
        result = (result + *lower) * 0.5F;
    }
    return result;
}

bool decodeGrayscale(const CameraFrame& frame, double processingScale,
                     cv::Mat* grayscale, QSize* sourceSize, QString* error)
{
    const std::vector<uchar> encoded(frame.jpeg.cbegin(), frame.jpeg.cend());
    cv::Mat decoded = cv::imdecode(encoded, cv::IMREAD_GRAYSCALE);
    if (decoded.empty()) {
        if (error)
            *error = QStringLiteral("OpenCV 无法解码跟踪帧 JPEG");
        return false;
    }
    applyCameraRotation(frame, &decoded);
    *sourceSize = QSize(decoded.cols, decoded.rows);
    if (processingScale < 0.999) {
        cv::resize(decoded, *grayscale, cv::Size(), processingScale,
                   processingScale, cv::INTER_AREA);
    } else {
        *grayscale = std::move(decoded);
    }
    return !grayscale->empty();
}

cv::Rect2f scaledTargetRect(const PersonDetection& target,
                            const cv::Size& processingSize)
{
    return cv::Rect2f(
        static_cast<float>(target.normalizedCenter.x()
                           - target.normalizedSize.width() * 0.5)
            * processingSize.width,
        static_cast<float>(target.normalizedCenter.y()
                           - target.normalizedSize.height() * 0.5)
            * processingSize.height,
        static_cast<float>(target.normalizedSize.width())
            * processingSize.width,
        static_cast<float>(target.normalizedSize.height())
            * processingSize.height);
}

cv::Rect integerRoi(const cv::Rect2f& value, const cv::Size& size)
{
    const cv::Rect bounds(0, 0, size.width, size.height);
    const cv::Rect rounded(
        static_cast<int>(std::floor(value.x)),
        static_cast<int>(std::floor(value.y)),
        static_cast<int>(std::ceil(value.width)),
        static_cast<int>(std::ceil(value.height)));
    return rounded & bounds;
}

void seedFeatures(const cv::Mat& image, const cv::Rect2f& target,
                  int maximumPoints, double qualityLevel,
                  double minimumDistance, std::vector<cv::Point2f>* points)
{
    const cv::Rect roi = integerRoi(target, image.size());
    if (roi.width < 4 || roi.height < 4)
        return;
    cv::Mat mask(image.size(), CV_8UC1, cv::Scalar(0));
    mask(roi).setTo(cv::Scalar(255));
    std::vector<cv::Point2f> candidates;
    cv::goodFeaturesToTrack(image, candidates, maximumPoints, qualityLevel,
                            minimumDistance, mask, 3, false, 0.04);
    for (const cv::Point2f& candidate : candidates) {
        const bool duplicate = std::any_of(
            points->cbegin(), points->cend(), [&](const cv::Point2f& existing) {
                return cv::norm(candidate - existing) < minimumDistance;
            });
        if (!duplicate)
            points->push_back(candidate);
        if (static_cast<int>(points->size()) >= maximumPoints)
            break;
    }
}

QRectF normalizedRect(const cv::Rect2f& value, const cv::Size& size)
{
    return QRectF(value.x / size.width, value.y / size.height,
                  value.width / size.width, value.height / size.height);
}
#endif
}

SparseOpticalFlowTrackerConfiguration
SparseOpticalFlowTrackerConfiguration::fromEnvironment()
{
    SparseOpticalFlowTrackerConfiguration configuration;
    configuration.processingScale = configuredDouble(
        "LONGPET_VISION_TRACKER_SCALE", 0.5, 0.25, 1.0);
    configuration.maximumPoints = configuredInteger(
        "LONGPET_VISION_TRACKER_MAX_POINTS", 60, 12, 200);
    configuration.minimumPoints = configuredInteger(
        "LONGPET_VISION_TRACKER_MIN_POINTS", 6, 3, 50);
    configuration.minimumPoints = std::min(
        configuration.minimumPoints, configuration.maximumPoints);
    configuration.qualityLevel = configuredDouble(
        "LONGPET_VISION_TRACKER_QUALITY", 0.01, 0.0001, 0.2);
    configuration.minimumDistance = configuredDouble(
        "LONGPET_VISION_TRACKER_MIN_DISTANCE", 5.0, 1.0, 30.0);
    configuration.maximumPointError = configuredDouble(
        "LONGPET_VISION_TRACKER_MAX_ERROR", 20.0, 1.0, 100.0);
    return configuration;
}

struct SparseOpticalFlowTracker::Impl {
#ifdef LONGPET_HAS_VISION
    cv::Mat previousGray;
    std::vector<cv::Point2f> previousPoints;
    cv::Rect2f targetRect;
    QSize sourceSize;
    float detectorConfidence = 0.0F;
#endif
};

SparseOpticalFlowTracker::SparseOpticalFlowTracker(
    SparseOpticalFlowTrackerConfiguration configuration)
    : m_impl(std::make_unique<Impl>()),
      m_configuration(std::move(configuration))
{
    m_info.trackerName = QStringLiteral("sparse-lk");
    m_info.processingScale = m_configuration.processingScale;
    m_info.maximumPoints = m_configuration.maximumPoints;
    m_info.minimumPoints = m_configuration.minimumPoints;
}

SparseOpticalFlowTracker::~SparseOpticalFlowTracker() = default;

bool SparseOpticalFlowTracker::start(const CameraFrame& frame,
                                     const PersonDetection& target,
                                     QString* error)
{
    reset();
    if (!frame.isValid() || target.normalizedSize.isEmpty()) {
        if (error)
            *error = QStringLiteral("跟踪器初始化输入无效");
        return false;
    }
#ifndef LONGPET_HAS_VISION
    if (error)
        *error = QStringLiteral("当前构建未启用 OpenCV 视觉支持");
    return false;
#else
    try {
        if (!decodeGrayscale(frame, m_configuration.processingScale,
                             &m_impl->previousGray, &m_impl->sourceSize,
                             error)) {
            return false;
        }
        m_impl->targetRect = scaledTargetRect(
            target, m_impl->previousGray.size());
        const cv::Rect clipped = integerRoi(
            m_impl->targetRect, m_impl->previousGray.size());
        if (clipped.width < 8 || clipped.height < 8) {
            if (error)
                *error = QStringLiteral("检测框过小，无法初始化跟踪器");
            reset();
            return false;
        }
        m_impl->targetRect = cv::Rect2f(clipped);
        seedFeatures(m_impl->previousGray, m_impl->targetRect,
                     m_configuration.maximumPoints,
                     m_configuration.qualityLevel,
                     m_configuration.minimumDistance,
                     &m_impl->previousPoints);
        if (static_cast<int>(m_impl->previousPoints.size())
            < m_configuration.minimumPoints) {
            if (error) {
                *error = QStringLiteral("检测框纹理不足：仅找到 %1 个特征点")
                             .arg(m_impl->previousPoints.size());
            }
            reset();
            return false;
        }
        m_impl->detectorConfidence = target.confidence;
        m_active = true;
        return true;
    } catch (const cv::Exception& exception) {
        if (error) {
            *error = QStringLiteral("稀疏光流初始化失败：%1")
                         .arg(QString::fromUtf8(exception.what()));
        }
        reset();
        return false;
    }
#endif
}

VisionTrackerResult SparseOpticalFlowTracker::update(
    const CameraFrame& frame, QString* error)
{
    VisionTrackerResult result;
    result.frameSequence = frame.sequence;
    result.timestamp = frame.timestamp;
    if (!m_active || !frame.isValid()) {
        if (error) {
            *error = !m_active ? QStringLiteral("跟踪器尚未初始化")
                              : QStringLiteral("跟踪帧无效");
        }
        return result;
    }
#ifndef LONGPET_HAS_VISION
    if (error)
        *error = QStringLiteral("当前构建未启用 OpenCV 视觉支持");
    return result;
#else
    QElapsedTimer totalTimer;
    QElapsedTimer stageTimer;
    totalTimer.start();
    stageTimer.start();
    try {
        cv::Mat currentGray;
        if (!decodeGrayscale(frame, m_configuration.processingScale,
                             &currentGray, &result.sourceSize, error)) {
            reset();
            return result;
        }
        result.decodeMs = stageTimer.nsecsElapsed() / 1'000'000.0;
        if (currentGray.size() != m_impl->previousGray.size()
            || result.sourceSize != m_impl->sourceSize) {
            if (error)
                *error = QStringLiteral("摄像头分辨率在跟踪期间发生变化");
            reset();
            return result;
        }

        stageTimer.restart();
        std::vector<cv::Point2f> nextPoints;
        std::vector<uchar> statuses;
        std::vector<float> errors;
        cv::calcOpticalFlowPyrLK(
            m_impl->previousGray, currentGray, m_impl->previousPoints,
            nextPoints, statuses, errors, cv::Size(15, 15), 2,
            cv::TermCriteria(cv::TermCriteria::COUNT
                                 | cv::TermCriteria::EPS,
                             20, 0.03));

        std::vector<cv::Point2f> previousValid;
        std::vector<cv::Point2f> currentValid;
        std::vector<float> dxValues;
        std::vector<float> dyValues;
        std::vector<float> pointErrors;
        for (size_t index = 0; index < statuses.size(); ++index) {
            if (!statuses[index]
                || errors[index] > m_configuration.maximumPointError
                || nextPoints[index].x < 0.0F || nextPoints[index].y < 0.0F
                || nextPoints[index].x >= currentGray.cols
                || nextPoints[index].y >= currentGray.rows) {
                continue;
            }
            previousValid.push_back(m_impl->previousPoints[index]);
            currentValid.push_back(nextPoints[index]);
            dxValues.push_back(nextPoints[index].x
                               - m_impl->previousPoints[index].x);
            dyValues.push_back(nextPoints[index].y
                               - m_impl->previousPoints[index].y);
            pointErrors.push_back(errors[index]);
        }
        if (static_cast<int>(currentValid.size())
            < m_configuration.minimumPoints) {
            if (error) {
                *error = QStringLiteral("有效光流点不足：%1/%2")
                             .arg(currentValid.size())
                             .arg(m_configuration.minimumPoints);
            }
            reset();
            return result;
        }

        const float dx = median(dxValues);
        const float dy = median(dyValues);
        const float residualLimit = std::max(
            4.0F, static_cast<float>(m_configuration.minimumDistance * 2.5));
        std::vector<cv::Point2f> inlierPrevious;
        std::vector<cv::Point2f> inlierCurrent;
        std::vector<float> inlierErrors;
        for (size_t index = 0; index < currentValid.size(); ++index) {
            const cv::Point2f displacement =
                currentValid[index] - previousValid[index];
            if (cv::norm(displacement - cv::Point2f(dx, dy))
                <= residualLimit) {
                inlierPrevious.push_back(previousValid[index]);
                inlierCurrent.push_back(currentValid[index]);
                inlierErrors.push_back(pointErrors[index]);
            }
        }
        if (static_cast<int>(inlierCurrent.size())
            < m_configuration.minimumPoints) {
            if (error)
                *error = QStringLiteral("光流一致性检查失败");
            reset();
            return result;
        }

        const cv::Point2f previousCenter(
            m_impl->targetRect.x + m_impl->targetRect.width * 0.5F,
            m_impl->targetRect.y + m_impl->targetRect.height * 0.5F);
        const cv::Point2f translatedCenter = previousCenter
            + cv::Point2f(dx, dy);
        std::vector<float> scaleRatios;
        for (size_t index = 0; index < inlierCurrent.size(); ++index) {
            const float before = cv::norm(inlierPrevious[index]
                                          - previousCenter);
            if (before < 3.0F)
                continue;
            const float after = cv::norm(inlierCurrent[index]
                                         - translatedCenter);
            scaleRatios.push_back(after / before);
        }
        const float scale = scaleRatios.size() >= 3
            ? std::clamp(median(scaleRatios), 0.92F, 1.08F) : 1.0F;
        cv::Rect2f candidate(
            translatedCenter.x - m_impl->targetRect.width * scale * 0.5F,
            translatedCenter.y - m_impl->targetRect.height * scale * 0.5F,
            m_impl->targetRect.width * scale,
            m_impl->targetRect.height * scale);
        const cv::Rect2f bounds(0.0F, 0.0F,
                               static_cast<float>(currentGray.cols),
                               static_cast<float>(currentGray.rows));
        const cv::Rect2f visible = candidate & bounds;
        if (candidate.area() <= 1.0F
            || visible.area() / candidate.area() < 0.35F) {
            if (error)
                *error = QStringLiteral("目标已离开有效画面区域");
            reset();
            return result;
        }
        m_impl->targetRect = visible;
        m_impl->previousGray = std::move(currentGray);
        m_impl->previousPoints = std::move(inlierCurrent);
        if (static_cast<int>(m_impl->previousPoints.size())
            < m_configuration.maximumPoints / 2) {
            seedFeatures(m_impl->previousGray, m_impl->targetRect,
                         m_configuration.maximumPoints,
                         m_configuration.qualityLevel,
                         m_configuration.minimumDistance,
                         &m_impl->previousPoints);
        }

        const float medianError = median(inlierErrors);
        const float retention = std::min(
            1.0F, static_cast<float>(m_impl->previousPoints.size())
                / std::max(1.0F,
                    static_cast<float>(m_configuration.maximumPoints) * 0.5F));
        const float quality = 1.0F - std::min(
            1.0F, medianError
                / static_cast<float>(m_configuration.maximumPointError));
        result.success = true;
        result.confidence = std::clamp(retention * quality, 0.0F, 1.0F);
        result.trackedPointCount = static_cast<int>(
            m_impl->previousPoints.size());
        result.target = VisionGeometry::personFromNormalizedRect(
            m_impl->detectorConfidence,
            normalizedRect(m_impl->targetRect, m_impl->previousGray.size()),
            result.sourceSize);
        result.trackingMs = stageTimer.nsecsElapsed() / 1'000'000.0;
        result.totalMs = totalTimer.nsecsElapsed() / 1'000'000.0;
        return result;
    } catch (const cv::Exception& exception) {
        if (error) {
            *error = QStringLiteral("稀疏光流更新失败：%1")
                         .arg(QString::fromUtf8(exception.what()));
        }
        reset();
        return result;
    }
#endif
}

void SparseOpticalFlowTracker::reset()
{
    m_active = false;
#ifdef LONGPET_HAS_VISION
    m_impl->previousGray.release();
    m_impl->previousPoints.clear();
    m_impl->targetRect = {};
    m_impl->sourceSize = {};
    m_impl->detectorConfidence = 0.0F;
#endif
}

bool SparseOpticalFlowTracker::isActive() const
{
    return m_active;
}

VisionTrackerInfo SparseOpticalFlowTracker::info() const
{
    return m_info;
}
