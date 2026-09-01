#include "model/CameraModels.h"
#include "model/VisionModels.h"
#include "platform/CameraCaptureAdapter.h"
#include "platform/FastestDetAdapter.h"
#include "platform/FastestDetPostProcessor.h"
#include "platform/VideoCallMediaAdapter.h"
#include "services/VisionPorts.h"
#include "services/VisionService.h"

#include <QMutex>
#include <QMutexLocker>
#include <QSignalSpy>
#include <QTest>
#include <QThread>

#include <atomic>
#include <vector>

namespace {
QByteArray jpegPayload(const QByteArray& body)
{
    return QByteArray::fromHex("ffd8") + body + QByteArray::fromHex("ffd9");
}

class TestCameraCaptureAdapter final : public CameraCaptureAdapter {
public:
    using CameraCaptureAdapter::CameraCaptureAdapter;

    void feed(const QByteArray& bytes) { ingestCameraBytes(bytes); }

    bool startSucceeds = true;
    int startCount = 0;
    int stopCount = 0;

protected:
    bool startCapture(QString* error) override
    {
        ++startCount;
        if (startSucceeds)
            return true;
        if (error)
            *error = QStringLiteral("fake camera unavailable");
        return false;
    }

    void stopCapture() override { ++stopCount; }
};

class FakeVisionDetector final : public VisionDetectorPort {
public:
    bool initialize(QString* error) override
    {
        if (!initializeSucceeds) {
            if (error)
                *error = QStringLiteral("fake model unavailable");
            return false;
        }
        available = true;
        return true;
    }

    bool isAvailable() const override { return available; }

    VisionFrameResult detect(const CameraFrame& frame, QString*) override
    {
        ++detectCount;
        if (delayMs > 0)
            QThread::msleep(static_cast<unsigned long>(delayMs));
        {
            QMutexLocker locker(&sequenceMutex);
            detectedSequences.append(frame.sequence);
        }
        VisionFrameResult result;
        result.frameSequence = frame.sequence;
        result.timestamp = frame.timestamp;
        result.sourceSize = QSize(640, 480);
        result.totalMs = delayMs;
        return result;
    }

    VisionDetectorInfo info() const override
    {
        VisionDetectorInfo value;
        value.modelPath = QStringLiteral("fake.onnx");
        value.inputShape = {1, 3, 352, 352};
        value.outputShape = {1, 85, 11, 11};
        return value;
    }

    QList<quint64> sequences() const
    {
        QMutexLocker locker(&sequenceMutex);
        return detectedSequences;
    }

    bool initializeSucceeds = true;
    bool available = false;
    int delayMs = 0;
    std::atomic<int> detectCount {0};
    mutable QMutex sequenceMutex;
    QList<quint64> detectedSequences;
};
}

class VisionV1Test final : public QObject {
    Q_OBJECT

private slots:
    void cameraSourceSharesLifecycleAndKeepsLatestJpeg();
    void cameraDeviceConfigurationPreservesLegacyFallback();
    void fastestDetPostprocessMapsFiltersAndNormalizesPerson();
    void visionServiceUsesLatestFrameOnlyAndPauses();
    void videoCallAndVisionShareOneCameraSource();
    void missingModelAndCameraDegradeWithoutCrash();
};

void VisionV1Test::cameraSourceSharesLifecycleAndKeepsLatestJpeg()
{
    TestCameraCaptureAdapter camera;
    QObject firstConsumer;
    auto* secondConsumer = new QObject;
    QSignalSpy frameSpy(&camera, &CameraSourcePort::frameReady);

    QString error;
    QVERIFY(camera.acquire(&firstConsumer, &error));
    QVERIFY(camera.acquire(secondConsumer, &error));
    QCOMPARE(camera.startCount, 1);
    QCOMPARE(camera.consumerCount(), 2);

    const QByteArray first = jpegPayload("first");
    const QByteArray second = jpegPayload("second");
    camera.feed(QByteArray("garbage") + first.left(4));
    QCOMPARE(frameSpy.count(), 0);
    camera.feed(first.mid(4) + second);
    QCOMPARE(frameSpy.count(), 2);
    QCOMPARE(camera.latestFrame().sequence, quint64(2));
    QCOMPARE(camera.latestFrame().jpeg, second);

    camera.release(&firstConsumer);
    QCOMPARE(camera.consumerCount(), 1);
    QCOMPARE(camera.stopCount, 0);
    delete secondConsumer;
    QCOMPARE(camera.consumerCount(), 0);
    QCOMPARE(camera.stopCount, 1);
}

void VisionV1Test::cameraDeviceConfigurationPreservesLegacyFallback()
{
    const QByteArray shared = qgetenv("LONGPET_CAMERA_DEVICE");
    const QByteArray legacy = qgetenv("LONGPET_CALL_CAMERA_DEVICE");
    qunsetenv("LONGPET_CAMERA_DEVICE");
    qputenv("LONGPET_CALL_CAMERA_DEVICE", "/dev/video-legacy");
    QCOMPARE(CameraCaptureAdapter::configuredDevice(),
             QStringLiteral("/dev/video-legacy"));
    qputenv("LONGPET_CAMERA_DEVICE", "/dev/video-shared");
    QCOMPARE(CameraCaptureAdapter::configuredDevice(),
             QStringLiteral("/dev/video-shared"));
    if (shared.isNull())
        qunsetenv("LONGPET_CAMERA_DEVICE");
    else
        qputenv("LONGPET_CAMERA_DEVICE", shared);
    if (legacy.isNull())
        qunsetenv("LONGPET_CALL_CAMERA_DEVICE");
    else
        qputenv("LONGPET_CALL_CAMERA_DEVICE", legacy);
}

void VisionV1Test::fastestDetPostprocessMapsFiltersAndNormalizesPerson()
{
    constexpr int channels = 7;
    constexpr int height = 2;
    constexpr int width = 2;
    constexpr int plane = height * width;
    std::vector<float> output(channels * plane, 0.0F);
    const int cell = 3;
    output[cell] = 0.9F;
    output[5 * plane + cell] = 0.9F;
    output[6 * plane + cell] = 0.1F;

    const QList<PersonDetection> persons = FastestDetPostProcessor::decode(
        output.data(), channels, height, width, QSize(640, 480),
        0.5F, 0.45F);
    QCOMPARE(persons.size(), 1);
    const PersonDetection person = persons.front();
    QVERIFY(qAbs(person.normalizedCenter.x() - 0.5) < 0.001);
    QVERIFY(qAbs(person.normalizedCenter.y() - 0.5) < 0.001);
    QVERIFY(qAbs(person.normalizedSize.width() - 0.5) < 0.001);
    QVERIFY(qAbs(person.boundingBox.x() - 160.0) < 0.01);
    QVERIFY(qAbs(person.boundingBox.y() - 120.0) < 0.01);
    QVERIFY(qAbs(person.boundingBox.width() - 320.0) < 0.01);
    QVERIFY(qAbs(person.boundingBox.height() - 240.0) < 0.01);

    QVERIFY(FastestDetPostProcessor::decode(
        output.data(), channels, height, width, QSize(640, 480),
        0.95F, 0.45F).isEmpty());
    output[6 * plane + cell] = 0.95F;
    QVERIFY(FastestDetPostProcessor::decode(
        output.data(), channels, height, width, QSize(640, 480),
        0.5F, 0.45F).isEmpty());
}

void VisionV1Test::visionServiceUsesLatestFrameOnlyAndPauses()
{
    TestCameraCaptureAdapter camera;
    FakeVisionDetector detector;
    detector.delayMs = 80;
    VisionService service(&camera, &detector, 1);
    QSignalSpy availableSpy(&service, &VisionService::availabilityChanged);
    QSignalSpy resultSpy(&service, &VisionService::visionResultReady);
    QSignalSpy pauseSpy(&service, &VisionService::pausedChanged);
    service.start();
    QTRY_VERIFY_WITH_TIMEOUT(service.isAvailable(), 1'000);
    QVERIFY(availableSpy.count() >= 1);

    camera.feed(jpegPayload("frame-1"));
    QTRY_VERIFY_WITH_TIMEOUT(detector.detectCount.load() >= 1, 500);
    for (int index = 2; index <= 20; ++index)
        camera.feed(jpegPayload(QByteArray::number(index)));
    QTRY_VERIFY_WITH_TIMEOUT(detector.detectCount.load() >= 2, 1'000);
    QTRY_VERIFY_WITH_TIMEOUT(detector.sequences().contains(20), 1'000);
    QVERIFY(detector.detectCount.load() < 6);
    QTRY_VERIFY_WITH_TIMEOUT(resultSpy.count() >= 2, 500);

    service.setVideoCallActive(true);
    QVERIFY(service.isPaused());
    const int countWhilePaused = detector.detectCount.load();
    camera.feed(jpegPayload("paused"));
    QTest::qWait(150);
    QCOMPARE(detector.detectCount.load(), countWhilePaused);
    service.setVideoCallActive(false);
    QVERIFY(!service.isPaused());
    camera.feed(jpegPayload("resumed"));
    QTRY_VERIFY_WITH_TIMEOUT(
        detector.detectCount.load() > countWhilePaused, 500);
    QVERIFY(pauseSpy.count() >= 2);

    service.stop();
    QCOMPARE(camera.consumerCount(), 0);
    QCOMPARE(camera.stopCount, 1);
}

void VisionV1Test::videoCallAndVisionShareOneCameraSource()
{
    const QByteArray originalPort = qgetenv("LONGPET_MEDIA_PORT");
    qputenv("LONGPET_MEDIA_PORT", "18789");
    TestCameraCaptureAdapter camera;
    FakeVisionDetector detector;
    VisionService vision(&camera, &detector, 1);
    vision.start();
    QTRY_VERIFY_WITH_TIMEOUT(vision.isAvailable(), 1'000);
    QCOMPARE(camera.consumerCount(), 1);

    VideoCallMediaAdapter media(&camera);
    VideoCallSnapshot snapshot;
    snapshot.callId = QStringLiteral("vision-share-test");
    snapshot.mediaToken = QStringLiteral("test-token");
    snapshot.mode = VideoCallMode::Video;
    QString error;
    QVERIFY2(media.prepare(snapshot, false, &error), qPrintable(error));
    QCOMPARE(camera.startCount, 1);
    QCOMPARE(camera.consumerCount(), 2);
    media.stop();
    QCOMPARE(camera.consumerCount(), 1);
    QCOMPARE(camera.stopCount, 0);
    vision.stop();
    QCOMPARE(camera.consumerCount(), 0);
    QCOMPARE(camera.stopCount, 1);

    if (originalPort.isNull())
        qunsetenv("LONGPET_MEDIA_PORT");
    else
        qputenv("LONGPET_MEDIA_PORT", originalPort);
}

void VisionV1Test::missingModelAndCameraDegradeWithoutCrash()
{
    FastestDetConfiguration missingConfiguration;
    missingConfiguration.modelPath = QStringLiteral(
        "definitely-missing-fastestdet.onnx");
    FastestDetAdapter missingModel(missingConfiguration);
    QString error;
    QVERIFY(!missingModel.initialize(&error));
    QVERIFY(error.contains(QStringLiteral("模型不存在")));

    TestCameraCaptureAdapter camera;
    camera.startSucceeds = false;
    FakeVisionDetector detector;
    VisionService service(&camera, &detector, 1);
    QSignalSpy failureSpy(&service, &VisionService::failed);
    service.start();
    QTRY_VERIFY_WITH_TIMEOUT(failureSpy.count() >= 1, 1'000);
    QVERIFY(!service.isAvailable());
    QVERIFY(service.isRunning());
    QCOMPARE(camera.consumerCount(), 0);
    service.stop();

    TestCameraCaptureAdapter unusedCamera;
    FakeVisionDetector unavailableDetector;
    unavailableDetector.initializeSucceeds = false;
    VisionService unavailableService(&unusedCamera, &unavailableDetector, 1);
    QSignalSpy unavailableSpy(&unavailableService, &VisionService::failed);
    unavailableService.start();
    QTRY_VERIFY_WITH_TIMEOUT(unavailableSpy.count() >= 1, 1'000);
    QCOMPARE(unusedCamera.startCount, 0);
    unavailableService.stop();
}

QTEST_GUILESS_MAIN(VisionV1Test)

#include "VisionV1Test.moc"
