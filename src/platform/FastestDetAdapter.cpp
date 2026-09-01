#include "FastestDetAdapter.h"

#include "FastestDetPostProcessor.h"

#include <QElapsedTimer>
#include <QFileInfo>

#include <algorithm>
#include <array>
#include <cstring>
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
template <typename TensorShapeInfo>
bool readFourDimensionalShape(const TensorShapeInfo& typeInfo,
                              std::array<int64_t, 4>* shape,
                              const QString& tensorDescription,
                              QString* error)
{
    const size_t dimensionCount = typeInfo.GetDimensionsCount();
    if (dimensionCount != shape->size()) {
        if (error) {
            *error = QStringLiteral("%1 必须是四维 Tensor，实际维度数为 %2")
                         .arg(tensorDescription)
                         .arg(dimensionCount);
        }
        return false;
    }

    // Do not use TensorTypeAndShapeInfo::GetShape() here. It allocates a
    // std::vector from the runtime-provided dimension count before the caller
    // can validate that count. Reading into a fixed array keeps a malformed or
    // ABI-incompatible model/runtime response from causing an unbounded
    // allocation on the embedded target.
    Ort::ThrowOnError(Ort::GetApi().GetDimensions(
        typeInfo, shape->data(), shape->size()));
    return true;
}
#endif
}

FastestDetConfiguration FastestDetConfiguration::fromEnvironment()
{
    FastestDetConfiguration configuration;
    configuration.modelPath =
        qEnvironmentVariable("LONGPET_VISION_MODEL_PATH").trimmed();
    if (configuration.modelPath.isEmpty()) {
#ifdef Q_OS_LINUX
        configuration.modelPath =
            QStringLiteral("/home/longpet/models/fastestdet.onnx");
#else
        configuration.modelPath = QStringLiteral("models/fastestdet.onnx");
#endif
    }
    configuration.confidenceThreshold = configuredFloat(
        "LONGPET_VISION_CONFIDENCE_THRESHOLD", 0.65F, 0.01F, 0.99F);
    configuration.nmsThreshold = configuredFloat(
        "LONGPET_VISION_NMS_THRESHOLD", 0.45F, 0.01F, 0.99F);
    configuration.inferenceThreads = configuredInteger(
        "LONGPET_VISION_INFERENCE_THREADS", 1, 1, 64);
    return configuration;
}

struct FastestDetAdapter::Impl {
#ifdef LONGPET_HAS_VISION
    Impl()
        : environment(ORT_LOGGING_LEVEL_WARNING, "LongPet.FastestDet")
    {
    }

    Ort::Env environment;
    std::unique_ptr<Ort::Session> session;
    std::string inputName;
    std::string outputName;
    int inputWidth = 0;
    int inputHeight = 0;
    int outputChannels = 0;
    int featureHeight = 0;
    int featureWidth = 0;
#endif
};

FastestDetAdapter::FastestDetAdapter(FastestDetConfiguration configuration)
    : m_impl(std::make_unique<Impl>()),
      m_configuration(std::move(configuration))
{
    m_info.modelPath = m_configuration.modelPath;
    m_info.provider = QStringLiteral("ONNX Runtime CPUExecutionProvider");
    m_info.inferenceThreads = m_configuration.inferenceThreads;
    m_info.confidenceThreshold = m_configuration.confidenceThreshold;
    m_info.nmsThreshold = m_configuration.nmsThreshold;
}

FastestDetAdapter::~FastestDetAdapter() = default;

bool FastestDetAdapter::initialize(QString* error)
{
    m_available = false;
    m_info.inputShape.clear();
    m_info.outputShape.clear();
    if (!QFileInfo::exists(m_configuration.modelPath)) {
        if (error) {
            *error = QStringLiteral("FastestDet 模型不存在：%1")
                         .arg(m_configuration.modelPath);
        }
        return false;
    }

#ifndef LONGPET_HAS_VISION
    if (error) {
        *error = QStringLiteral(
            "当前构建未启用 OpenCV/ONNX Runtime 视觉支持");
    }
    return false;
#else
    QString initializationStage = QStringLiteral("创建 ONNX Runtime session");
    try {
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(m_configuration.inferenceThreads);
        options.SetInterOpNumThreads(1);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        const QByteArray modelPath = QFileInfo(m_configuration.modelPath)
                                         .absoluteFilePath().toUtf8();
        m_impl->session = std::make_unique<Ort::Session>(
            m_impl->environment, modelPath.constData(), options);

        initializationStage = QStringLiteral("检查模型输入输出数量");
        if (m_impl->session->GetInputCount() != 1
            || m_impl->session->GetOutputCount() != 1) {
            if (error) {
                *error = QStringLiteral(
                    "FastestDet graph 必须恰好包含一个输入和一个输出");
            }
            m_impl->session.reset();
            return false;
        }

        Ort::AllocatorWithDefaultOptions allocator;
        auto inputName = m_impl->session->GetInputNameAllocated(0, allocator);
        auto outputName = m_impl->session->GetOutputNameAllocated(0, allocator);
        m_impl->inputName = inputName.get();
        m_impl->outputName = outputName.get();

        initializationStage = QStringLiteral("读取模型 Tensor 信息");
        // GetTensorTypeAndShapeInfo() returns a non-owning view for TypeInfo.
        // Keep the owning TypeInfo objects alive until all shape and element
        // type queries are complete.
        const auto inputTypeInfo = m_impl->session->GetInputTypeInfo(0);
        const auto outputTypeInfo = m_impl->session->GetOutputTypeInfo(0);
        const auto inputType = inputTypeInfo.GetTensorTypeAndShapeInfo();
        const auto outputType = outputTypeInfo.GetTensorTypeAndShapeInfo();
        std::array<int64_t, 4> inputShape {};
        std::array<int64_t, 4> outputShape {};
        if (!readFourDimensionalShape(inputType, &inputShape,
                                      QStringLiteral("模型输入"), error)
            || !readFourDimensionalShape(outputType, &outputShape,
                                         QStringLiteral("模型输出"), error)) {
            m_impl->session.reset();
            return false;
        }
        initializationStage = QStringLiteral("校验 FastestDet graph");
        if (inputType.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT
            || outputType.GetElementType()
                != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT
            || inputShape[0] != 1 || inputShape[1] != 3
            || inputShape[2] != 352 || inputShape[3] != 352
            || outputShape[0] != 1 || outputShape[1] < 6
            || outputShape[2] <= 0 || outputShape[3] <= 0) {
            if (error) {
                *error = QStringLiteral(
                    "模型 graph 与 FastestDet 不匹配；期望输入 [1,3,352,352] "
                    "及单尺度 NCHW 输出");
            }
            m_impl->session.reset();
            return false;
        }

        m_impl->inputHeight = static_cast<int>(inputShape[2]);
        m_impl->inputWidth = static_cast<int>(inputShape[3]);
        m_impl->outputChannels = static_cast<int>(outputShape[1]);
        m_impl->featureHeight = static_cast<int>(outputShape[2]);
        m_impl->featureWidth = static_cast<int>(outputShape[3]);
        for (const int64_t dimension : inputShape)
            m_info.inputShape.append(dimension);
        for (const int64_t dimension : outputShape)
            m_info.outputShape.append(dimension);
        m_info.runtimeVersion = QString::fromLatin1(Ort::GetVersionString());
        m_available = true;
        return true;
    } catch (const Ort::Exception& exception) {
        m_impl->session.reset();
        if (error) {
            *error = QStringLiteral("加载 FastestDet ONNX 失败（%1）：%2")
                         .arg(initializationStage,
                              QString::fromUtf8(exception.what()));
        }
        return false;
    } catch (const std::exception& exception) {
        m_impl->session.reset();
        if (error) {
            *error = QStringLiteral("初始化 FastestDet 失败（%1）：%2")
                         .arg(initializationStage,
                              QString::fromUtf8(exception.what()));
        }
        return false;
    }
#endif
}

bool FastestDetAdapter::isAvailable() const
{
    return m_available;
}

VisionFrameResult FastestDetAdapter::detect(const CameraFrame& frame,
                                            QString* error)
{
    VisionFrameResult result;
    result.frameSequence = frame.sequence;
    result.timestamp = frame.timestamp;
    if (!m_available || !frame.isValid()) {
        if (error) {
            *error = !m_available ? QStringLiteral("FastestDet 当前不可用")
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
        cv::Mat resized;
        cv::resize(decoded, resized,
                   cv::Size(m_impl->inputWidth, m_impl->inputHeight),
                   0.0, 0.0, cv::INTER_AREA);
        cv::Mat normalized;
        resized.convertTo(normalized, CV_32FC3, 1.0 / 255.0);
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
                *error = QStringLiteral("FastestDet 没有返回有效 Tensor");
            return result;
        }

        const auto outputType = outputs.front().GetTensorTypeAndShapeInfo();
        std::array<int64_t, 4> shape {};
        if (!readFourDimensionalShape(outputType, &shape,
                                      QStringLiteral("运行时模型输出"), error)) {
            return result;
        }
        if (shape[0] != 1
            || shape[1] != m_impl->outputChannels
            || shape[2] != m_impl->featureHeight
            || shape[3] != m_impl->featureWidth) {
            if (error)
                *error = QStringLiteral("FastestDet 运行时输出 shape 发生变化");
            return result;
        }
        stageTimer.restart();
        result.persons = FastestDetPostProcessor::decode(
            outputs.front().GetTensorData<float>(), m_impl->outputChannels,
            m_impl->featureHeight, m_impl->featureWidth, result.sourceSize,
            m_configuration.confidenceThreshold,
            m_configuration.nmsThreshold);
        result.postprocessMs = stageTimer.nsecsElapsed() / 1'000'000.0;
        result.totalMs = totalTimer.nsecsElapsed() / 1'000'000.0;
        return result;
    } catch (const Ort::Exception& exception) {
        if (error) {
            *error = QStringLiteral("FastestDet inference 失败：%1")
                         .arg(QString::fromUtf8(exception.what()));
        }
    } catch (const cv::Exception& exception) {
        if (error) {
            *error = QStringLiteral("FastestDet 图像处理失败：%1")
                         .arg(QString::fromUtf8(exception.what()));
        }
    } catch (const std::exception& exception) {
        if (error) {
            *error = QStringLiteral("FastestDet 处理失败：%1")
                         .arg(QString::fromUtf8(exception.what()));
        }
    }
    result.totalMs = 0.0;
    return result;
#endif
}

VisionDetectorInfo FastestDetAdapter::info() const
{
    return m_info;
}
