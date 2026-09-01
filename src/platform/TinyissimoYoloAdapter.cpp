#include "TinyissimoYoloAdapter.h"

#include "TinyissimoYoloPostProcessor.h"

#include <QElapsedTimer>
#include <QFileInfo>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <utility>
#include <vector>

#ifdef LONGPET_HAS_VISION
#include <onnxruntime_cxx_api.h>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#endif

namespace {
float configuredFloat(const char* name, float fallback,
                      float minimum, float maximum)
{
    bool valid = false;
    const double value = qEnvironmentVariable(name).toDouble(&valid);
    if (!valid || value < minimum || value > maximum)
        return fallback;
    return static_cast<float>(value);
}

int configuredInteger(const char* name, int fallback,
                      int minimum, int maximum)
{
    bool valid = false;
    const int value = qEnvironmentVariableIntValue(name, &valid);
    if (!valid || value < minimum || value > maximum)
        return fallback;
    return value;
}

#ifdef LONGPET_HAS_VISION
template <size_t DimensionCount, typename TensorShapeInfo>
bool readFixedShape(const TensorShapeInfo& typeInfo,
                    std::array<int64_t, DimensionCount>* shape,
                    const QString& description, QString* error)
{
    if (typeInfo.GetDimensionsCount() != DimensionCount) {
        if (error) {
            *error = QStringLiteral("%1 必须是 %2 维 Tensor，实际为 %3 维")
                         .arg(description)
                         .arg(DimensionCount)
                         .arg(typeInfo.GetDimensionsCount());
        }
        return false;
    }
    Ort::ThrowOnError(Ort::GetApi().GetDimensions(
        typeInfo, shape->data(), shape->size()));
    return true;
}

qint64 parameterCountFromMetadata(Ort::Session& session)
{
    try {
        Ort::AllocatorWithDefaultOptions allocator;
        const auto metadata = session.GetModelMetadata();
        auto value = metadata.LookupCustomMetadataMapAllocated(
            "longpet.parameter_count", allocator);
        if (!value)
            return 0;
        bool valid = false;
        const qint64 count = QString::fromUtf8(value.get()).toLongLong(&valid);
        return valid && count > 0 ? count : 0;
    } catch (const Ort::Exception&) {
        return 0;
    }
}
#endif
}

TinyissimoYoloConfiguration TinyissimoYoloConfiguration::fromEnvironment()
{
    TinyissimoYoloConfiguration configuration;
    configuration.modelPath =
        qEnvironmentVariable("LONGPET_VISION_MODEL_PATH").trimmed();
    if (configuration.modelPath.isEmpty()) {
#ifdef Q_OS_LINUX
        configuration.modelPath = QStringLiteral(
            "/home/longpet/models/tinyissimo-yolo-v1-small-person-128.onnx");
#else
        configuration.modelPath = QStringLiteral(
            "models/tinyissimo-yolo-v1-small-person-128.onnx");
#endif
    }
    configuration.confidenceThreshold = configuredFloat(
        "LONGPET_VISION_CONFIDENCE_THRESHOLD", 0.25F, 0.01F, 0.99F);
    configuration.nmsThreshold = configuredFloat(
        "LONGPET_VISION_NMS_THRESHOLD", 0.45F, 0.01F, 0.99F);
    configuration.inferenceThreads = configuredInteger(
        "LONGPET_VISION_INFERENCE_THREADS", 1, 1, 64);
    return configuration;
}

struct TinyissimoYoloAdapter::Impl {
#ifdef LONGPET_HAS_VISION
    Impl()
        : environment(ORT_LOGGING_LEVEL_WARNING, "LongPet.TinyissimoYOLO")
    {
    }

    Ort::Env environment;
    std::unique_ptr<Ort::Session> session;
    std::string inputName;
    std::string outputName;
    int inputWidth = 0;
    int inputHeight = 0;
    int candidateCount = 0;
    bool channelFirst = true;
#endif
};

TinyissimoYoloAdapter::TinyissimoYoloAdapter(
    TinyissimoYoloConfiguration configuration)
    : m_impl(std::make_unique<Impl>()),
      m_configuration(std::move(configuration))
{
    m_info.detectorName = QStringLiteral("tinyissimo-yolo-v1-small-person");
    m_info.modelPath = m_configuration.modelPath;
    m_info.provider = QStringLiteral("ONNX Runtime CPUExecutionProvider");
    m_info.inferenceThreads = m_configuration.inferenceThreads;
    m_info.confidenceThreshold = m_configuration.confidenceThreshold;
    m_info.nmsThreshold = m_configuration.nmsThreshold;
}

TinyissimoYoloAdapter::~TinyissimoYoloAdapter() = default;

bool TinyissimoYoloAdapter::initialize(QString* error)
{
    m_available = false;
    m_info.inputShape.clear();
    m_info.outputShape.clear();
    const QFileInfo modelFile(m_configuration.modelPath);
    if (!modelFile.exists()) {
        if (error) {
            *error = QStringLiteral("TinyissimoYOLO 模型不存在：%1")
                         .arg(m_configuration.modelPath);
        }
        return false;
    }
    m_info.modelBytes = modelFile.size();

#ifndef LONGPET_HAS_VISION
    if (error)
        *error = QStringLiteral("当前构建未启用 OpenCV/ONNX Runtime 视觉支持");
    return false;
#else
    QString stage = QStringLiteral("创建 ONNX Runtime session");
    try {
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(m_configuration.inferenceThreads);
        options.SetInterOpNumThreads(1);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        const QByteArray modelPath = modelFile.absoluteFilePath().toUtf8();
        m_impl->session = std::make_unique<Ort::Session>(
            m_impl->environment, modelPath.constData(), options);

        stage = QStringLiteral("检查模型输入输出");
        if (m_impl->session->GetInputCount() != 1
            || m_impl->session->GetOutputCount() != 1) {
            if (error) {
                *error = QStringLiteral(
                    "TinyissimoYOLO graph 必须恰好包含一个输入和一个输出");
            }
            m_impl->session.reset();
            return false;
        }

        Ort::AllocatorWithDefaultOptions allocator;
        auto inputName = m_impl->session->GetInputNameAllocated(0, allocator);
        auto outputName = m_impl->session->GetOutputNameAllocated(0, allocator);
        m_impl->inputName = inputName.get();
        m_impl->outputName = outputName.get();

        stage = QStringLiteral("校验 TinyissimoYOLO graph");
        const auto inputTypeInfo = m_impl->session->GetInputTypeInfo(0);
        const auto outputTypeInfo = m_impl->session->GetOutputTypeInfo(0);
        const auto inputType = inputTypeInfo.GetTensorTypeAndShapeInfo();
        const auto outputType = outputTypeInfo.GetTensorTypeAndShapeInfo();
        std::array<int64_t, 4> inputShape {};
        std::array<int64_t, 3> outputShape {};
        if (!readFixedShape(inputType, &inputShape,
                            QStringLiteral("模型输入"), error)
            || !readFixedShape(outputType, &outputShape,
                               QStringLiteral("模型输出"), error)) {
            m_impl->session.reset();
            return false;
        }
        const bool channelFirst = outputShape[1] == 5
            && outputShape[2] > 0;
        const bool candidateFirst = outputShape[2] == 5
            && outputShape[1] > 0;
        const int candidateCount = static_cast<int>(
            channelFirst ? outputShape[2] : outputShape[1]);
        const int expectedCandidateCount = static_cast<int>(
            (inputShape[2] / 32) * (inputShape[3] / 32));
        if (inputType.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT
            || outputType.GetElementType()
                != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT
            || inputShape[0] != 1 || inputShape[1] != 3
            || inputShape[2] <= 0 || inputShape[3] <= 0
            || inputShape[2] != inputShape[3]
            || inputShape[2] % 32 != 0
            || outputShape[0] != 1
            || (!channelFirst && !candidateFirst)
            || candidateCount != expectedCandidateCount) {
            if (error) {
                *error = QStringLiteral(
                    "graph 与 TinyissimoYOLO person-only 不匹配；期望静态 "
                    "float32 [1,3,H,W]（H/W 为 32 的倍数）及 [1,5,N] "
                    "或 [1,N,5] 输出");
            }
            m_impl->session.reset();
            return false;
        }

        m_impl->inputHeight = static_cast<int>(inputShape[2]);
        m_impl->inputWidth = static_cast<int>(inputShape[3]);
        m_impl->channelFirst = channelFirst;
        m_impl->candidateCount = candidateCount;
        for (const int64_t dimension : inputShape)
            m_info.inputShape.append(dimension);
        for (const int64_t dimension : outputShape)
            m_info.outputShape.append(dimension);
        m_info.runtimeVersion = QString::fromLatin1(Ort::GetVersionString());
        m_info.parameterCount = parameterCountFromMetadata(*m_impl->session);
        m_available = true;
        return true;
    } catch (const Ort::Exception& exception) {
        m_impl->session.reset();
        if (error) {
            *error = QStringLiteral("加载 TinyissimoYOLO ONNX 失败（%1）：%2")
                         .arg(stage, QString::fromUtf8(exception.what()));
        }
    } catch (const std::exception& exception) {
        m_impl->session.reset();
        if (error) {
            *error = QStringLiteral("初始化 TinyissimoYOLO 失败（%1）：%2")
                         .arg(stage, QString::fromUtf8(exception.what()));
        }
    }
    return false;
#endif
}

bool TinyissimoYoloAdapter::isAvailable() const
{
    return m_available;
}

VisionFrameResult TinyissimoYoloAdapter::detect(const CameraFrame& frame,
                                                 QString* error)
{
    VisionFrameResult result;
    result.frameSequence = frame.sequence;
    result.timestamp = frame.timestamp;
    if (!m_available || !frame.isValid()) {
        if (error) {
            *error = !m_available
                ? QStringLiteral("TinyissimoYOLO 当前不可用")
                : QStringLiteral("摄像头 JPEG 帧无效");
        }
        return result;
    }

#ifndef LONGPET_HAS_VISION
    if (error)
        *error = QStringLiteral("当前构建未启用视觉支持");
    return result;
#else
    try {
        QElapsedTimer totalTimer;
        totalTimer.start();
        QElapsedTimer stageTimer;
        stageTimer.start();
        const std::vector<uchar> encoded(frame.jpeg.cbegin(), frame.jpeg.cend());
        const cv::Mat decoded = cv::imdecode(encoded, cv::IMREAD_COLOR);
        result.decodeMs = stageTimer.nsecsElapsed() / 1'000'000.0;
        if (decoded.empty()) {
            if (error)
                *error = QStringLiteral("OpenCV 无法解码摄像头 JPEG");
            return result;
        }
        result.sourceSize = QSize(decoded.cols, decoded.rows);

        stageTimer.restart();
        const float scale = std::min(
            static_cast<float>(m_impl->inputWidth) / decoded.cols,
            static_cast<float>(m_impl->inputHeight) / decoded.rows);
        const int resizedWidth = std::max(
            1, static_cast<int>(std::round(decoded.cols * scale)));
        const int resizedHeight = std::max(
            1, static_cast<int>(std::round(decoded.rows * scale)));
        const float halfWidthPadding =
            (m_impl->inputWidth - resizedWidth) / 2.0F;
        const float halfHeightPadding =
            (m_impl->inputHeight - resizedHeight) / 2.0F;
        const int left = static_cast<int>(std::round(halfWidthPadding - 0.1F));
        const int right = static_cast<int>(std::round(halfWidthPadding + 0.1F));
        const int top = static_cast<int>(std::round(halfHeightPadding - 0.1F));
        const int bottom = static_cast<int>(std::round(halfHeightPadding + 0.1F));
        cv::Mat resized;
        cv::resize(decoded, resized, cv::Size(resizedWidth, resizedHeight),
                   0.0, 0.0, scale < 1.0F ? cv::INTER_AREA : cv::INTER_LINEAR);
        cv::Mat letterboxed;
        cv::copyMakeBorder(resized, letterboxed, top, bottom, left, right,
                           cv::BORDER_CONSTANT, cv::Scalar(114, 114, 114));
        if (letterboxed.cols != m_impl->inputWidth
            || letterboxed.rows != m_impl->inputHeight) {
            if (error)
                *error = QStringLiteral("TinyissimoYOLO letterbox 尺寸异常");
            return result;
        }
        cv::Mat rgb;
        cv::cvtColor(letterboxed, rgb, cv::COLOR_BGR2RGB);
        cv::Mat normalized;
        rgb.convertTo(normalized, CV_32FC3, 1.0 / 255.0);
        const size_t plane = static_cast<size_t>(m_impl->inputWidth)
            * static_cast<size_t>(m_impl->inputHeight);
        std::vector<float> input(plane * 3);
        std::vector<cv::Mat> channels {
            cv::Mat(m_impl->inputHeight, m_impl->inputWidth, CV_32F,
                    input.data()),
            cv::Mat(m_impl->inputHeight, m_impl->inputWidth, CV_32F,
                    input.data() + plane),
            cv::Mat(m_impl->inputHeight, m_impl->inputWidth, CV_32F,
                    input.data() + plane * 2)
        };
        cv::split(normalized, channels);
        result.preprocessMs = stageTimer.nsecsElapsed() / 1'000'000.0;

        const std::array<int64_t, 4> inputShape {
            1, 3, m_impl->inputHeight, m_impl->inputWidth
        };
        Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(
            OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            memoryInfo, input.data(), input.size(), inputShape.data(),
            inputShape.size());
        const char* inputNames[] {m_impl->inputName.c_str()};
        const char* outputNames[] {m_impl->outputName.c_str()};
        stageTimer.restart();
        std::vector<Ort::Value> outputs = m_impl->session->Run(
            Ort::RunOptions {nullptr}, inputNames, &inputTensor, 1,
            outputNames, 1);
        result.inferenceMs = stageTimer.nsecsElapsed() / 1'000'000.0;
        if (outputs.size() != 1 || !outputs.front().IsTensor()) {
            if (error)
                *error = QStringLiteral("TinyissimoYOLO 没有返回有效 Tensor");
            return result;
        }

        const auto outputType = outputs.front().GetTensorTypeAndShapeInfo();
        std::array<int64_t, 3> outputShape {};
        if (!readFixedShape(outputType, &outputShape,
                            QStringLiteral("运行时模型输出"), error)) {
            return result;
        }
        const int runtimeCandidates = static_cast<int>(
            m_impl->channelFirst ? outputShape[2] : outputShape[1]);
        const int runtimeChannels = static_cast<int>(
            m_impl->channelFirst ? outputShape[1] : outputShape[2]);
        if (outputShape[0] != 1 || runtimeChannels != 5
            || runtimeCandidates != m_impl->candidateCount) {
            if (error)
                *error = QStringLiteral("TinyissimoYOLO 运行时输出 shape 发生变化");
            return result;
        }

        stageTimer.restart();
        TinyissimoLetterboxTransform transform;
        transform.sourceSize = result.sourceSize;
        transform.inputSize = QSize(m_impl->inputWidth, m_impl->inputHeight);
        transform.scale = scale;
        transform.padX = static_cast<float>(left);
        transform.padY = static_cast<float>(top);
        result.persons = TinyissimoYoloPostProcessor::decode(
            outputs.front().GetTensorData<float>(), m_impl->candidateCount,
            m_impl->channelFirst, transform,
            m_configuration.confidenceThreshold,
            m_configuration.nmsThreshold);
        result.postprocessMs = stageTimer.nsecsElapsed() / 1'000'000.0;
        result.totalMs = totalTimer.nsecsElapsed() / 1'000'000.0;
        return result;
    } catch (const Ort::Exception& exception) {
        if (error) {
            *error = QStringLiteral("TinyissimoYOLO inference 失败：%1")
                         .arg(QString::fromUtf8(exception.what()));
        }
    } catch (const cv::Exception& exception) {
        if (error) {
            *error = QStringLiteral("TinyissimoYOLO 图像处理失败：%1")
                         .arg(QString::fromUtf8(exception.what()));
        }
    } catch (const std::exception& exception) {
        if (error) {
            *error = QStringLiteral("TinyissimoYOLO 处理失败：%1")
                         .arg(QString::fromUtf8(exception.what()));
        }
    }
    result.totalMs = 0.0;
    return result;
#endif
}

VisionDetectorInfo TinyissimoYoloAdapter::info() const
{
    return m_info;
}
