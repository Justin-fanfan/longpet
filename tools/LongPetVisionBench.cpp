#include "model/CameraModels.h"
#include "model/VisionModels.h"
#include "platform/CameraCaptureAdapter.h"
#include "platform/FastestDetAdapter.h"
#include "platform/TinyissimoYoloAdapter.h"
#include "services/VisionPorts.h"
#include "services/VisionService.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <memory>
#include <vector>

#if defined(Q_OS_LINUX) || defined(__linux__)
#include <unistd.h>
#endif

namespace {
QString shapeText(const QList<qint64>& shape)
{
    QStringList dimensions;
    for (const qint64 dimension : shape)
        dimensions.append(QString::number(dimension));
    return QStringLiteral("[%1]").arg(dimensions.join(QLatin1Char(',')));
}

void printDetectorInfo(const VisionDetectorInfo& info,
                       const QString& cameraDevice = {})
{
    qInfo().noquote() << "detector=" << info.detectorName;
    qInfo().noquote() << "model_path=" << info.modelPath;
    qInfo().noquote() << "model_bytes=" << info.modelBytes;
    qInfo().noquote() << "parameter_count=" << info.parameterCount;
    qInfo().noquote() << "model_input_shape=" << shapeText(info.inputShape);
    qInfo().noquote() << "model_output_shape=" << shapeText(info.outputShape);
    qInfo().noquote() << "provider=" << info.provider;
    qInfo().noquote() << "onnx_runtime_version=" << info.runtimeVersion;
    qInfo().noquote() << "inference_threads=" << info.inferenceThreads;
    qInfo().noquote() << "confidence_threshold=" << info.confidenceThreshold;
    qInfo().noquote() << "nms_threshold=" << info.nmsThreshold;
    if (info.inputShape.size() == 4) {
        qInfo().noquote() << "input_resolution="
                          << QStringLiteral("%1x%2")
                                 .arg(info.inputShape[3])
                                 .arg(info.inputShape[2]);
    }
    if (!cameraDevice.isEmpty())
        qInfo().noquote() << "camera_device=" << cameraDevice;
}

void printFrame(const VisionFrameResult& result, bool warmup)
{
    qInfo().noquote()
        << QStringLiteral(
               "frame=%1 warmup=%2 decode_ms=%3 preprocess_ms=%4 "
               "inference_ms=%5 postprocess_ms=%6 total_ms=%7 person_count=%8")
               .arg(result.frameSequence)
               .arg(warmup ? QStringLiteral("true") : QStringLiteral("false"))
               .arg(result.decodeMs, 0, 'f', 3)
               .arg(result.preprocessMs, 0, 'f', 3)
               .arg(result.inferenceMs, 0, 'f', 3)
               .arg(result.postprocessMs, 0, 'f', 3)
               .arg(result.totalMs, 0, 'f', 3)
               .arg(result.persons.size());
}

double percentile(std::vector<double> values, double percentileValue)
{
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    const size_t index = static_cast<size_t>(std::ceil(
        percentileValue * static_cast<double>(values.size()))) - 1;
    return values[std::min(index, values.size() - 1)];
}

double average(const std::vector<double>& values)
{
    if (values.empty())
        return 0.0;
    return std::accumulate(values.begin(), values.end(), 0.0)
        / static_cast<double>(values.size());
}

QString linuxMemoryValue(const QByteArray& key)
{
#if defined(Q_OS_LINUX) || defined(__linux__)
    const QString statusPath = QStringLiteral("/proc/%1/status")
                                   .arg(QCoreApplication::applicationPid());
    QFile status(statusPath);
    if (!status.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QStringLiteral("unavailable (%1)")
            .arg(status.errorString());
    }
    QByteArray prefix = key;
    prefix.append(':');
    // procfs reports a file size of zero, so QFile::atEnd() can be true before
    // the first read. Read until readLine() itself signals EOF instead.
    while (true) {
        const QByteArray rawLine = status.readLine();
        if (rawLine.isEmpty())
            break;
        const QByteArray line = rawLine.trimmed();
        if (line.startsWith(prefix))
            return QString::fromLatin1(line.mid(prefix.size()).trimmed());
    }
#else
    Q_UNUSED(key)
#endif
    return QStringLiteral("unavailable (status key missing)");
}

double processCpuSeconds()
{
#if defined(Q_OS_LINUX) || defined(__linux__)
    QFile stat(QStringLiteral("/proc/self/stat"));
    if (!stat.open(QIODevice::ReadOnly | QIODevice::Text))
        return -1.0;
    const QByteArray line = stat.readAll().trimmed();
    const qsizetype commandEnd = line.lastIndexOf(')');
    if (commandEnd < 0)
        return -1.0;
    const QList<QByteArray> fields = line.mid(commandEnd + 2).split(' ');
    // fields[0] is field 3 (state), so utime/stime fields 14/15 are 11/12.
    if (fields.size() <= 12)
        return -1.0;
    bool userValid = false;
    bool systemValid = false;
    const qint64 userTicks = fields[11].toLongLong(&userValid);
    const qint64 systemTicks = fields[12].toLongLong(&systemValid);
    const long ticksPerSecond = sysconf(_SC_CLK_TCK);
    if (!userValid || !systemValid || ticksPerSecond <= 0)
        return -1.0;
    return static_cast<double>(userTicks + systemTicks) / ticksPerSecond;
#else
    return -1.0;
#endif
}

void printSummary(const std::vector<VisionFrameResult>& results,
                   int warmupCount, double runtimeSeconds,
                   double measurementSeconds, double cpuStartSeconds,
                   double cpuEndSeconds)
{
    std::vector<double> decode;
    std::vector<double> preprocess;
    std::vector<double> inference;
    std::vector<double> postprocess;
    std::vector<double> total;
    decode.reserve(results.size());
    preprocess.reserve(results.size());
    inference.reserve(results.size());
    postprocess.reserve(results.size());
    total.reserve(results.size());
    for (const VisionFrameResult& result : results) {
        decode.push_back(result.decodeMs);
        preprocess.push_back(result.preprocessMs);
        inference.push_back(result.inferenceMs);
        postprocess.push_back(result.postprocessMs);
        total.push_back(result.totalMs);
    }

    qInfo().noquote() << "summary_warmup_count=" << warmupCount;
    qInfo().noquote() << "summary_measured_frames=" << results.size();
    if (!inference.empty()) {
        const auto [minimum, maximum] = std::minmax_element(
            inference.cbegin(), inference.cend());
        qInfo().noquote() << "summary_avg_decode_ms=" << average(decode);
        qInfo().noquote() << "summary_avg_preprocess_ms="
                          << average(preprocess);
        qInfo().noquote() << "summary_avg_inference_ms=" << average(inference);
        qInfo().noquote() << "summary_avg_postprocess_ms="
                          << average(postprocess);
        qInfo().noquote() << "summary_min_inference_ms=" << *minimum;
        qInfo().noquote() << "summary_max_inference_ms=" << *maximum;
        qInfo().noquote() << "summary_p50_inference_ms="
                          << percentile(inference, 0.50);
        qInfo().noquote() << "summary_p95_inference_ms="
                          << percentile(inference, 0.95);
        qInfo().noquote() << "summary_avg_total_ms=" << average(total);
        qInfo().noquote() << "summary_p50_total_ms="
                          << percentile(total, 0.50);
        qInfo().noquote() << "summary_p95_total_ms="
                          << percentile(total, 0.95);
    }
    qInfo().noquote() << "summary_effective_inference_fps="
                      << (measurementSeconds > 0.0
                              ? results.size() / measurementSeconds : 0.0);
    qInfo().noquote() << "summary_runtime_seconds=" << runtimeSeconds;
    qInfo().noquote() << "summary_measurement_seconds="
                      << measurementSeconds;
    qInfo().noquote() << "summary_rss=" << linuxMemoryValue("VmRSS");
    qInfo().noquote() << "summary_peak_rss=" << linuxMemoryValue("VmHWM");
    const double cpuPercent = measurementSeconds > 0.0
        && cpuStartSeconds >= 0.0 && cpuEndSeconds >= cpuStartSeconds
        ? (cpuEndSeconds - cpuStartSeconds) / measurementSeconds * 100.0
        : -1.0;
    qInfo().noquote() << "summary_process_cpu_percent="
                      << (cpuPercent >= 0.0
                              ? QString::number(cpuPercent, 'f', 2)
                              : QStringLiteral("unavailable"));
}

struct BenchmarkDetectorConfiguration {
    QString detector;
    QString modelPath;
    float confidenceThreshold = 0.25F;
    float nmsThreshold = 0.45F;
    int inferenceThreads = 1;
};

std::unique_ptr<VisionDetectorPort> createDetector(
    const BenchmarkDetectorConfiguration& configuration)
{
    if (configuration.detector == QStringLiteral("fastestdet")) {
        FastestDetConfiguration fastest;
        fastest.modelPath = configuration.modelPath;
        fastest.confidenceThreshold = configuration.confidenceThreshold;
        fastest.nmsThreshold = configuration.nmsThreshold;
        fastest.inferenceThreads = configuration.inferenceThreads;
        return std::make_unique<FastestDetAdapter>(fastest);
    }
    TinyissimoYoloConfiguration tinyissimo;
    tinyissimo.modelPath = configuration.modelPath;
    tinyissimo.confidenceThreshold = configuration.confidenceThreshold;
    tinyissimo.nmsThreshold = configuration.nmsThreshold;
    tinyissimo.inferenceThreads = configuration.inferenceThreads;
    return std::make_unique<TinyissimoYoloAdapter>(tinyissimo);
}

int positiveInteger(const QCommandLineParser& parser,
                    const QCommandLineOption& option,
                    int fallback)
{
    bool valid = false;
    const int value = parser.value(option).toInt(&valid);
    return valid && value > 0 ? value : fallback;
}

int nonNegativeInteger(const QCommandLineParser& parser,
                       const QCommandLineOption& option,
                       int fallback)
{
    bool valid = false;
    const int value = parser.value(option).toInt(&valid);
    return valid && value >= 0 ? value : fallback;
}

int runImageBenchmark(const BenchmarkDetectorConfiguration& configuration,
                      const QString& imagePath,
                      int warmupCount,
                      int iterations)
{
    QFile image(imagePath);
    if (!image.open(QIODevice::ReadOnly)) {
        qCritical().noquote() << "image_error=" << image.errorString();
        return 2;
    }
    const QByteArray jpeg = image.readAll();
    std::unique_ptr<VisionDetectorPort> detector = createDetector(configuration);
    QString error;
    if (!detector->initialize(&error)) {
        qCritical().noquote() << "model_error=" << error;
        return 2;
    }
    printDetectorInfo(detector->info());
    qInfo().noquote() << "mode=image image_path="
                      << QFileInfo(imagePath).absoluteFilePath();

    CameraFrame frame;
    frame.jpeg = jpeg;
    frame.timestamp = QDateTime::currentDateTimeUtc();
    std::vector<VisionFrameResult> measured;
    measured.reserve(static_cast<size_t>(iterations));
    QElapsedTimer totalRuntime;
    QElapsedTimer measurementRuntime;
    double cpuStartSeconds = -1.0;
    totalRuntime.start();
    for (int index = 0; index < warmupCount + iterations; ++index) {
        if (index == warmupCount) {
            measurementRuntime.start();
            cpuStartSeconds = processCpuSeconds();
        }
        frame.sequence = static_cast<quint64>(index + 1);
        frame.timestamp = QDateTime::currentDateTimeUtc();
        const VisionFrameResult result = detector->detect(frame, &error);
        if (!error.isEmpty()) {
            qCritical().noquote() << "inference_error=" << error;
            return 3;
        }
        const bool warmup = index < warmupCount;
        printFrame(result, warmup);
        if (!warmup)
            measured.push_back(result);
    }
    const double cpuEndSeconds = processCpuSeconds();
    printSummary(measured, warmupCount,
                 totalRuntime.nsecsElapsed() / 1'000'000'000.0,
                 measurementRuntime.isValid()
                     ? measurementRuntime.nsecsElapsed() / 1'000'000'000.0
                     : 0.0,
                 cpuStartSeconds, cpuEndSeconds);
    return 0;
}

int runCameraBenchmark(const BenchmarkDetectorConfiguration& configuration,
                       const QString& cameraDevice,
                       int warmupCount,
                       int durationSeconds,
                       int intervalMs)
{
    qputenv("LONGPET_CAMERA_DEVICE", cameraDevice.toUtf8());
    CameraCaptureAdapter camera;
    std::unique_ptr<VisionDetectorPort> detector = createDetector(configuration);
    VisionService service(&camera, detector.get(), intervalMs);
    QEventLoop loop;
    std::vector<VisionFrameResult> measured;
    int receivedCount = 0;
    int exitCode = 0;
    QElapsedTimer totalRuntime;
    QElapsedTimer measurementRuntime;
    double cpuStartSeconds = -1.0;

    QObject::connect(&service, &VisionService::detectorInfoReady,
                     &loop, [&](const VisionDetectorInfo& info) {
        printDetectorInfo(info, cameraDevice);
        qInfo().noquote() << "mode=camera duration_seconds="
                          << durationSeconds;
    });
    QObject::connect(&service, &VisionService::availabilityChanged,
                     &loop, [&](bool available, const QString& message) {
        qInfo().noquote() << "vision_available=" << available
                          << "message=" << message;
        if (available && !totalRuntime.isValid()) {
            totalRuntime.start();
            if (warmupCount == 0) {
                measurementRuntime.start();
                cpuStartSeconds = processCpuSeconds();
            }
        }
    });
    QObject::connect(&service, &VisionService::visionResultReady,
                     &loop, [&](const VisionFrameResult& result) {
        const bool warmup = receivedCount++ < warmupCount;
        printFrame(result, warmup);
        if (!warmup) {
            measured.push_back(result);
        } else if (receivedCount == warmupCount
                   && !measurementRuntime.isValid()) {
            measurementRuntime.start();
            cpuStartSeconds = processCpuSeconds();
        }
    });
    QObject::connect(&service, &VisionService::failed,
                     &loop, [&](const QString& stage, const QString& message) {
        qCritical().noquote() << "vision_error_stage=" << stage
                              << "message=" << message;
        exitCode = 3;
        loop.quit();
    });
    QTimer::singleShot(durationSeconds * 1'000, &loop, &QEventLoop::quit);
    service.start();
    loop.exec();
    const double runtimeSeconds = totalRuntime.isValid()
        ? totalRuntime.nsecsElapsed() / 1'000'000'000.0 : 0.0;
    const double measurementSeconds = measurementRuntime.isValid()
        ? measurementRuntime.nsecsElapsed() / 1'000'000'000.0 : 0.0;
    const double cpuEndSeconds = processCpuSeconds();
    service.stop();
    if (exitCode != 0)
        return exitCode;
    if (measured.empty()) {
        qCritical().noquote() << "benchmark_error=no measured camera frames";
        return 4;
    }
    printSummary(measured, std::min(receivedCount, warmupCount),
                 runtimeSeconds, measurementSeconds,
                 cpuStartSeconds, cpuEndSeconds);
    return 0;
}
}

int main(int argc, char* argv[])
{
    // Qt builds with journald support otherwise redirect qInfo away from a
    // non-interactive SSH pipe, making automated benchmark collection empty.
    qputenv("QT_LOGGING_TO_CONSOLE", "1");
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("LongPetVisionBench"));
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("LongPet detector image/camera benchmark"));
    parser.addHelpOption();

    const QCommandLineOption detectorOption(
        {QStringLiteral("d"), QStringLiteral("detector")},
        QStringLiteral("Detector: fastestdet or tinyissimo"),
        QStringLiteral("name"), QStringLiteral("tinyissimo"));
    const QCommandLineOption modelOption(
        {QStringLiteral("m"), QStringLiteral("model")},
        QStringLiteral("ONNX model path"), QStringLiteral("path"));
    const QCommandLineOption imageOption(
        {QStringLiteral("i"), QStringLiteral("image")},
        QStringLiteral("JPEG image benchmark input"), QStringLiteral("path"));
    const QCommandLineOption cameraOption(
        {QStringLiteral("c"), QStringLiteral("camera")},
        QStringLiteral("USB camera benchmark device"),
        QStringLiteral("device"));
    const QCommandLineOption iterationsOption(
        QStringLiteral("iterations"), QStringLiteral("Measured image runs"),
        QStringLiteral("count"), QStringLiteral("100"));
    const QCommandLineOption warmupOption(
        QStringLiteral("warmup"), QStringLiteral("Warmup runs/frames"),
        QStringLiteral("count"), QStringLiteral("10"));
    const QCommandLineOption durationOption(
        QStringLiteral("duration"), QStringLiteral("Camera duration in seconds"),
        QStringLiteral("seconds"), QStringLiteral("60"));
    const QCommandLineOption intervalOption(
        QStringLiteral("interval-ms"),
        QStringLiteral("Minimum camera inference interval"),
        QStringLiteral("milliseconds"), QStringLiteral("1"));
    const QCommandLineOption thresholdOption(
        QStringLiteral("threshold"), QStringLiteral("Confidence threshold"),
        QStringLiteral("value"));
    const QCommandLineOption nmsOption(
        QStringLiteral("nms-threshold"), QStringLiteral("NMS IoU threshold"),
        QStringLiteral("value"), QStringLiteral("0.45"));
    const QCommandLineOption threadsOption(
        QStringLiteral("threads"), QStringLiteral("ORT intra-op threads"),
        QStringLiteral("count"), QStringLiteral("1"));
    parser.addOptions({detectorOption, modelOption, imageOption, cameraOption,
                       iterationsOption, warmupOption, durationOption,
                       intervalOption, thresholdOption, nmsOption,
                       threadsOption});
    parser.process(application);

    if (!parser.isSet(modelOption)
        || parser.isSet(imageOption) == parser.isSet(cameraOption)) {
        parser.showHelp(1);
    }

    BenchmarkDetectorConfiguration configuration;
    configuration.detector = parser.value(detectorOption).trimmed().toLower();
    if (configuration.detector != QStringLiteral("fastestdet")
        && configuration.detector != QStringLiteral("tinyissimo")) {
        qCritical().noquote()
            << "argument_error=--detector must be fastestdet or tinyissimo";
        return 1;
    }
    configuration.modelPath = parser.value(modelOption);
    configuration.confidenceThreshold = configuration.detector
            == QStringLiteral("fastestdet") ? 0.65F : 0.25F;
    bool valid = false;
    if (parser.isSet(thresholdOption)) {
        const float threshold = parser.value(thresholdOption).toFloat(&valid);
        if (!valid || threshold <= 0.0F || threshold >= 1.0F) {
            qCritical().noquote()
                << "argument_error=--threshold must be between 0 and 1";
            return 1;
        }
        configuration.confidenceThreshold = threshold;
    }
    const float nms = parser.value(nmsOption).toFloat(&valid);
    if (valid && nms > 0.0F && nms < 1.0F)
        configuration.nmsThreshold = nms;
    configuration.inferenceThreads = positiveInteger(
        parser, threadsOption, 1);

    const int warmup = nonNegativeInteger(parser, warmupOption, 10);
    if (parser.isSet(imageOption)) {
        return runImageBenchmark(
            configuration, parser.value(imageOption), warmup,
            positiveInteger(parser, iterationsOption, 100));
    }
    return runCameraBenchmark(
        configuration, parser.value(cameraOption), warmup,
        positiveInteger(parser, durationOption, 60),
        positiveInteger(parser, intervalOption, 1));
}
