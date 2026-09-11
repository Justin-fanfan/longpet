#include "model/CameraModels.h"
#include "model/VisionModels.h"
#include "platform/CameraCaptureAdapter.h"
#include "platform/FastestDetAdapter.h"
#include "platform/FastestDetPostProcessor.h"
#include "platform/FamilyVisionProtocol.h"
#include "platform/FamilyVisionStreamAdapter.h"
#include "platform/TinyissimoYoloAdapter.h"
#include "platform/TinyissimoYoloPostProcessor.h"
#include "platform/VisionDetectorFactory.h"
#include "platform/VideoCallMediaAdapter.h"
#include "services/VisionPorts.h"
#include "services/VisionService.h"
#include "services/FamilyVisionMonitorService.h"
#include "services/FamilyVisionPorts.h"
#include "services/KwsPorts.h"
#include "services/MediaSessionCoordinator.h"
#include "services/VideoCallService.h"

#include <QMutex>
#include <QMutexLocker>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>
#include <QThread>
#include <QWebSocket>

#include <atomic>
#include <memory>
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
        if (returnPerson.load()) {
            result.persons.append(VisionGeometry::personFromNormalizedRect(
                0.85F, QRectF(0.30, 0.10, 0.35, 0.75), result.sourceSize));
        }
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
    std::atomic<bool> returnPerson {false};
    mutable QMutex sequenceMutex;
    QList<quint64> detectedSequences;
};

class FakeVisionTracker final : public VisionTrackerPort {
public:
    bool start(const CameraFrame&, const PersonDetection& target,
               QString* error) override
    {
        ++startCount;
        if (!startSucceeds.load()) {
            if (error)
                *error = QStringLiteral("fake tracker start failure");
            active = false;
            return false;
        }
        trackedTarget = target;
        active = true;
        return true;
    }

    VisionTrackerResult update(const CameraFrame& frame,
                               QString* error) override
    {
        ++updateCount;
        VisionTrackerResult result;
        result.frameSequence = frame.sequence;
        result.timestamp = frame.timestamp;
        result.sourceSize = QSize(640, 480);
        result.totalMs = 2.0;
        result.trackingMs = 1.0;
        if (!active || !updateSucceeds.load()) {
            if (error)
                *error = QStringLiteral("fake tracker lost target");
            active = false;
            return result;
        }
        result.success = true;
        result.target = trackedTarget;
        result.confidence = 0.8F;
        result.trackedPointCount = 20;
        return result;
    }

    void reset() override { active = false; ++resetCount; }
    bool isActive() const override { return active; }
    VisionTrackerInfo info() const override
    {
        VisionTrackerInfo value;
        value.trackerName = QStringLiteral("fake-tracker");
        value.maximumPoints = 30;
        value.minimumPoints = 5;
        return value;
    }

    PersonDetection trackedTarget;
    std::atomic<bool> startSucceeds {true};
    std::atomic<bool> updateSucceeds {true};
    std::atomic<int> startCount {0};
    std::atomic<int> updateCount {0};
    std::atomic<int> resetCount {0};
    bool active = false;
};

class DelayedKwsPort final : public KwsPort {
public:
    void start() override { running = true; }
    void pause() override { ++pauseCount; }
    void resume() override { paused = false; }
    void stop() override { running = false; emit kwsStopped(); }
    bool isRunning() const override { return running; }
    bool isPaused() const override { return paused; }
    void acknowledge() { paused = true; emit kwsPaused(); }
    int pauseCount = 0;
    bool running = true;
    bool paused = false;
};

class VisionAwareCallMedia final : public VideoCallMediaPort {
public:
    VisionAwareCallMedia(CameraSourcePort* camera, VisionService* vision)
        : camera(camera), vision(vision) {}
    quint16 port() const override { return 8788; }
    bool prepare(const VideoCallSnapshot& snapshot, bool audioEnabled, QString* error) override
    {
        pausedAtPrepare = vision->isPaused();
        audio = audioEnabled;
        if (failPrepare) {
            if (error)
                *error = QStringLiteral("test media preparation failure");
            return false;
        }
        if (snapshot.mode == VideoCallMode::Video)
            acquired = camera->acquire(this, error);
        return snapshot.mode == VideoCallMode::Voice || acquired;
    }
    void enableAudio() override { audio = true; }
    void stop() override
    {
        if (acquired)
            camera->release(this);
        acquired = audio = false;
    }
    CameraSourcePort* camera;
    VisionService* vision;
    bool failPrepare = false;
    bool pausedAtPrepare = false;
    bool acquired = false;
    bool audio = false;
};

class TestCallPrompt final : public CallPromptPlayerPort {
public:
    bool play(VideoCallMode, QString*) override { playing = true; return true; }
    void stop() override { playing = false; }
    void complete() { playing = false; emit finished(); }
    bool playing = false;
};

class FakeFamilyVisionStream final : public FamilyVisionStreamPort {
public:
    using FamilyVisionStreamPort::FamilyVisionStreamPort;

    bool start(QHostAddress, quint16 requestedPort, QString*) override
    {
        started = true;
        listeningPort = requestedPort == 0 ? 18'790 : requestedPort;
        return true;
    }
    void stop() override
    {
        if (viewer)
            emit viewerStopped(QStringLiteral("test stop"));
        viewer = false;
        started = false;
    }
    quint16 port() const override { return listeningPort; }
    FamilyVisionSession createSession(int frameRate, QString* error) override
    {
        if (!started || viewer) {
            if (error)
                *error = viewer ? QStringLiteral("已有家属正在查看 AI 视野")
                                : QStringLiteral("not started");
            return {};
        }
        FamilyVisionSession value;
        value.sessionId = QStringLiteral("fake-session");
        value.token = QStringLiteral("fake-token");
        value.port = listeningPort;
        value.frameRate = frameRate;
        value.expiresAt = QDateTime::currentDateTimeUtc().addSecs(30);
        return value;
    }
    void acceptViewer(const QString& sessionId) override
    {
        acceptedSession = sessionId;
        viewer = true;
    }
    void rejectViewer(const QString&, const QString& code,
                      const QString& message) override
    {
        rejectedCode = code;
        rejectedMessage = message;
        viewer = false;
    }
    void publishCameraFrame(const CameraFrame& frame) override
    {
        frames.append(frame);
    }
    void publishTelemetry(const FamilyVisionTelemetry& value) override
    {
        telemetry.append(value);
    }
    bool hasViewer() const override { return viewer; }
    void requestViewer(const QString& sessionId)
    {
        emit viewerStartRequested(sessionId);
    }
    void disconnectViewer()
    {
        viewer = false;
        emit viewerStopped(QStringLiteral("test disconnect"));
    }

    QList<CameraFrame> frames;
    QList<FamilyVisionTelemetry> telemetry;
    QString acceptedSession;
    QString rejectedCode;
    QString rejectedMessage;
    quint16 listeningPort = 0;
    bool started = false;
    bool viewer = false;
};
}

class VisionV1Test final : public QObject {
    Q_OBJECT

private slots:
    void cameraSourceSharesLifecycleAndKeepsLatestJpeg();
    void cameraDeviceConfigurationPreservesLegacyFallback();
    void fastestDetPostprocessMapsFiltersAndNormalizesPerson();
    void tinyissimoPostprocessMapsLetterboxAndAppliesNms();
    void detectorFactoryDefaultsToTinyissimoAndKeepsFastestDetFallback();
    void visionServiceUsesLatestFrameOnlyAndPauses();
    void visionServiceTracksCorrectsLosesAndReacquires();
    void videoCallAndVisionShareOneCameraSource();
    void familyAiViewSharesCameraWithoutPausingVision();
    void familyAiViewProtocolSerializesSafeNormalizedTargets();
    void familyAiViewWebSocketRequiresEphemeralAuthentication();
    void kwsGatedCallsPreserveVisionLifecycle();
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

void VisionV1Test::tinyissimoPostprocessMapsLetterboxAndAppliesNms()
{
    constexpr int candidateCount = 3;
    // Tinyissimo export layout: [1, 5, N] = cx, cy, w, h, class score.
    std::vector<float> output(5 * candidateCount, 0.0F);
    output[0 * candidateCount + 0] = 64.0F;
    output[1 * candidateCount + 0] = 64.0F;
    output[2 * candidateCount + 0] = 64.0F;
    output[3 * candidateCount + 0] = 64.0F;
    output[4 * candidateCount + 0] = 0.90F;
    // Nearly identical lower-confidence candidate must be suppressed.
    output[0 * candidateCount + 1] = 65.0F;
    output[1 * candidateCount + 1] = 64.0F;
    output[2 * candidateCount + 1] = 64.0F;
    output[3 * candidateCount + 1] = 64.0F;
    output[4 * candidateCount + 1] = 0.80F;
    output[4 * candidateCount + 2] = 0.10F;

    TinyissimoLetterboxTransform transform;
    transform.sourceSize = QSize(640, 480);
    transform.inputSize = QSize(128, 128);
    transform.scale = 0.2F;
    transform.padY = 16.0F;
    const QList<PersonDetection> persons =
        TinyissimoYoloPostProcessor::decode(
            output.data(), candidateCount, true, transform, 0.25F, 0.45F);
    QCOMPARE(persons.size(), 1);
    const PersonDetection person = persons.front();
    QVERIFY(qAbs(person.confidence - 0.90F) < 0.001F);
    QVERIFY(qAbs(person.boundingBox.x() - 160.0) < 0.01);
    QVERIFY(qAbs(person.boundingBox.y() - 80.0) < 0.01);
    QVERIFY(qAbs(person.boundingBox.width() - 320.0) < 0.01);
    QVERIFY(qAbs(person.boundingBox.height() - 320.0) < 0.01);
    QVERIFY(qAbs(person.normalizedCenter.x() - 0.5) < 0.001);
    QVERIFY(qAbs(person.normalizedCenter.y() - 0.5) < 0.001);

    QVERIFY(TinyissimoYoloPostProcessor::decode(
        output.data(), candidateCount, true, transform,
        0.95F, 0.45F).isEmpty());
}

void VisionV1Test::detectorFactoryDefaultsToTinyissimoAndKeepsFastestDetFallback()
{
    const QByteArray previous = qgetenv("LONGPET_VISION_DETECTOR");
    const bool wasSet = qEnvironmentVariableIsSet("LONGPET_VISION_DETECTOR");

    qunsetenv("LONGPET_VISION_DETECTOR");
    const std::unique_ptr<VisionDetectorPort> defaultDetector =
        VisionDetectorFactory::createFromEnvironment();
    QCOMPARE(defaultDetector->info().detectorName,
             QStringLiteral("tinyissimo-yolo-v1-small-person"));

    qputenv("LONGPET_VISION_DETECTOR", "fastestdet");
    const std::unique_ptr<VisionDetectorPort> fallbackDetector =
        VisionDetectorFactory::createFromEnvironment();
    QCOMPARE(fallbackDetector->info().detectorName,
             QStringLiteral("fastestdet"));

    if (wasSet)
        qputenv("LONGPET_VISION_DETECTOR", previous);
    else
        qunsetenv("LONGPET_VISION_DETECTOR");
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

void VisionV1Test::visionServiceTracksCorrectsLosesAndReacquires()
{
    TestCameraCaptureAdapter camera;
    FakeVisionDetector detector;
    detector.returnPerson = true;
    FakeVisionTracker tracker;
    VisionTrackingConfiguration configuration;
    configuration.trackerIntervalMs = 1;
    configuration.searchDetectorIntervalMs = 1;
    configuration.lostDetectorIntervalMs = 1;
    configuration.detectorCorrectionIntervalMs = 500;
    configuration.freshnessTimeoutMs = 1'000;
    VisionService service(&camera, &detector, &tracker, configuration);
    QSignalSpy observations(&service, &VisionService::targetObservationReady);
    QSignalSpy transitions(&service, &VisionService::trackingTransition);

    const auto containsStatus = [](const QSignalSpy& spy,
                                   TargetTrackingStatus expected) {
        for (const QList<QVariant>& arguments : spy) {
            if (arguments.front().value<TargetObservation>().status == expected)
                return true;
        }
        return false;
    };

    service.start();
    QTRY_VERIFY_WITH_TIMEOUT(service.isAvailable(), 1'000);
    camera.feed(jpegPayload("detected"));
    QTRY_VERIFY_WITH_TIMEOUT(
        containsStatus(observations, TargetTrackingStatus::Detected), 500);
    QCOMPARE(detector.detectCount.load(), 1);
    QCOMPARE(tracker.startCount.load(), 1);

    QTest::qWait(525);
    camera.feed(jpegPayload("corrected"));
    QTRY_VERIFY_WITH_TIMEOUT(
        containsStatus(observations, TargetTrackingStatus::Corrected), 500);
    QVERIFY(detector.detectCount.load() >= 2);
    QVERIFY(tracker.startCount.load() >= 2);

    camera.feed(jpegPayload("tracked"));
    QTRY_VERIFY_WITH_TIMEOUT(
        containsStatus(observations, TargetTrackingStatus::Tracking), 500);
    QVERIFY(tracker.updateCount.load() >= 1);

    tracker.updateSucceeds = false;
    camera.feed(jpegPayload("lost"));
    QTRY_VERIFY_WITH_TIMEOUT(
        containsStatus(observations, TargetTrackingStatus::Lost), 500);
    tracker.updateSucceeds = true;
    camera.feed(jpegPayload("reacquired"));
    QTRY_VERIFY_WITH_TIMEOUT(
        containsStatus(observations, TargetTrackingStatus::Reacquired), 500);

    const int observationsBeforePause = observations.count();
    service.setVideoCallActive(true);
    camera.feed(jpegPayload("during-call"));
    QTest::qWait(30);
    QCOMPARE(observations.count(), observationsBeforePause);
    service.setVideoCallActive(false);
    camera.feed(jpegPayload("after-call"));
    QTRY_VERIFY_WITH_TIMEOUT(observations.count() > observationsBeforePause,
                             500);
    QVERIFY(containsStatus(observations,
                           TargetTrackingStatus::Reacquired));
    QVERIFY(transitions.count() >= 5);
    service.stop();
    QCOMPARE(camera.consumerCount(), 0);
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

void VisionV1Test::familyAiViewSharesCameraWithoutPausingVision()
{
    TestCameraCaptureAdapter camera;
    FakeVisionDetector detector;
    detector.returnPerson = false;
    VisionService vision(&camera, &detector, 1);
    FakeFamilyVisionStream stream;
    FamilyVisionMonitorService monitor(&camera, &vision, &stream, 10);

    vision.start();
    QTRY_VERIFY_WITH_TIMEOUT(vision.isAvailable(), 1'000);
    QString error;
    QVERIFY(monitor.start(QHostAddress::LocalHost, 0, &error));
    const FamilyVisionSession session = monitor.createSession(&error);
    QVERIFY2(session.isValid(), qPrintable(error));
    stream.requestViewer(session.sessionId);
    QCOMPARE(stream.acceptedSession, session.sessionId);
    QCOMPARE(camera.consumerCount(), 2);
    QCOMPARE(camera.startCount, 1);
    QVERIFY(!vision.isPaused());

    camera.feed(jpegPayload("ai-view-without-target"));
    QTRY_VERIFY_WITH_TIMEOUT(!stream.frames.isEmpty(), 500);
    QVERIFY(!vision.isPaused());

    detector.returnPerson = true;
    QTest::qWait(110);
    camera.feed(jpegPayload("ai-view-person"));
    QTRY_VERIFY_WITH_TIMEOUT(!stream.telemetry.isEmpty()
        && stream.telemetry.constLast().observation.present, 1'000);
    QVERIFY(!vision.isPaused());

    stream.disconnectViewer();
    QCOMPARE(camera.consumerCount(), 1);
    QCOMPARE(camera.stopCount, 0);

    for (int cycle = 0; cycle < 5; ++cycle) {
        const FamilyVisionSession next = monitor.createSession(&error);
        QVERIFY2(next.isValid(), qPrintable(error));
        stream.requestViewer(next.sessionId);
        QCOMPARE(camera.consumerCount(), 2);
        stream.disconnectViewer();
        QCOMPARE(camera.consumerCount(), 1);
    }

    monitor.stop();
    vision.stop();
    QCOMPARE(camera.consumerCount(), 0);
    QCOMPARE(camera.stopCount, 1);
}

void VisionV1Test::familyAiViewProtocolSerializesSafeNormalizedTargets()
{
    FamilyVisionTelemetry telemetry;
    telemetry.detectorName = QStringLiteral("TinyissimoYOLO-v1.2");
    telemetry.trackerName = QStringLiteral("Sparse LK");
    telemetry.targetUpdateHz = 7.23;
    telemetry.observation.present = true;
    telemetry.observation.fresh = true;
    telemetry.observation.status = TargetTrackingStatus::Tracking;
    telemetry.observation.frameSequence = 1718;
    telemetry.observation.ageMs = 49;
    telemetry.observation.target = VisionGeometry::personFromNormalizedRect(
        0.86F, QRectF(0.75, -0.1, 0.5, 0.7), QSize(640, 480));
    telemetry.observation.detectorConfidence = 0.86F;
    telemetry.observation.trackerConfidence = 0.93F;
    telemetry.observation.trackedPointCount = 49;

    const QJsonObject object = FamilyVisionProtocol::telemetryObject(telemetry);
    QCOMPARE(object.value(QStringLiteral("protocol_version")).toInt(), 1);
    QCOMPARE(object.value(QStringLiteral("state")).toString(),
             QStringLiteral("TRACKING"));
    const QJsonObject box = object.value(QStringLiteral("bbox")).toObject();
    QVERIFY(qAbs(box.value(QStringLiteral("x")).toDouble() - 0.75) < 0.0001);
    QVERIFY(qAbs(box.value(QStringLiteral("y")).toDouble()) < 0.0001);
    QVERIFY(qAbs(box.value(QStringLiteral("w")).toDouble() - 0.25) < 0.0001);
    QVERIFY(qAbs(box.value(QStringLiteral("h")).toDouble() - 0.6) < 0.0001);

    telemetry.observation.status = TargetTrackingStatus::Lost;
    QVERIFY(FamilyVisionProtocol::telemetryObject(telemetry)
                .value(QStringLiteral("bbox")).isNull());
    telemetry.observation.status = TargetTrackingStatus::Searching;
    QVERIFY(FamilyVisionProtocol::telemetryObject(telemetry)
                .value(QStringLiteral("bbox")).isNull());
    telemetry.observation.status = TargetTrackingStatus::Tracking;
    telemetry.observation.fresh = false;
    QVERIFY(FamilyVisionProtocol::telemetryObject(telemetry)
                .value(QStringLiteral("bbox")).isNull());
}

void VisionV1Test::familyAiViewWebSocketRequiresEphemeralAuthentication()
{
    FamilyVisionStreamAdapter adapter;
    QString error;
    QVERIFY2(adapter.start(QHostAddress::LocalHost, 0, &error), qPrintable(error));
    FamilyVisionSession session = adapter.createSession(7, &error);
    QVERIFY2(session.isValid(), qPrintable(error));

    QWebSocket unauthenticated;
    QSignalSpy unauthConnected(&unauthenticated, &QWebSocket::connected);
    QSignalSpy unauthFrames(&unauthenticated, &QWebSocket::binaryMessageReceived);
    QSignalSpy unauthClosed(&unauthenticated, &QWebSocket::disconnected);
    unauthenticated.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/vision-monitor/v1")
                                  .arg(adapter.port())));
    QTRY_COMPARE_WITH_TIMEOUT(unauthConnected.count(), 1, 1'000);
    CameraFrame frame {jpegPayload("private"), 1,
                       QDateTime::currentDateTimeUtc()};
    adapter.publishCameraFrame(frame);
    QTest::qWait(50);
    QCOMPARE(unauthFrames.count(), 0);
    const QJsonObject wrongAuth {
        {QStringLiteral("type"), QStringLiteral("authenticate")},
        {QStringLiteral("protocol_version"), 1},
        {QStringLiteral("session_id"), session.sessionId},
        {QStringLiteral("token"), QStringLiteral("wrong-token")}
    };
    unauthenticated.sendBinaryMessage(MediaFrameProtocol::encode(
        MediaStreamType::Control, 1, 1,
        QJsonDocument(wrongAuth).toJson(QJsonDocument::Compact)));
    QTRY_COMPARE_WITH_TIMEOUT(unauthClosed.count(), 1, 1'000);

    session = adapter.createSession(7, &error);
    QVERIFY2(session.isValid(), qPrintable(error));
    QWebSocket authenticated;
    QSignalSpy connected(&authenticated, &QWebSocket::connected);
    QSignalSpy frames(&authenticated, &QWebSocket::binaryMessageReceived);
    QSignalSpy startRequests(&adapter,
        &FamilyVisionStreamPort::viewerStartRequested);
    QSignalSpy stopped(&adapter, &FamilyVisionStreamPort::viewerStopped);
    authenticated.open(QUrl(QStringLiteral(
        "ws://127.0.0.1:%1/vision-monitor/v1").arg(adapter.port())));
    QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 1'000);
    const QJsonObject auth {
        {QStringLiteral("type"), QStringLiteral("authenticate")},
        {QStringLiteral("protocol_version"), 1},
        {QStringLiteral("session_id"), session.sessionId},
        {QStringLiteral("token"), session.token}
    };
    authenticated.sendBinaryMessage(MediaFrameProtocol::encode(
        MediaStreamType::Control, 1, 1,
        QJsonDocument(auth).toJson(QJsonDocument::Compact)));
    QTRY_COMPARE_WITH_TIMEOUT(startRequests.count(), 1, 1'000);
    adapter.acceptViewer(session.sessionId);
    QTRY_VERIFY_WITH_TIMEOUT(adapter.hasViewer(), 500);
    frames.clear();
    adapter.publishCameraFrame(frame);
    QTRY_VERIFY_WITH_TIMEOUT(frames.count() >= 1, 1'000);
    bool foundVideo = false;
    for (const QList<QVariant>& arguments : frames) {
        MediaFrame decoded;
        if (MediaFrameProtocol::decode(arguments.front().toByteArray(), &decoded)
            && decoded.streamType == MediaStreamType::DeviceVideo) {
            foundVideo = true;
        }
    }
    QVERIFY(foundVideo);
    authenticated.close();
    QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 1'000);
    QVERIFY(!adapter.hasViewer());
    adapter.stop();
}

void VisionV1Test::kwsGatedCallsPreserveVisionLifecycle()
{
    TestCameraCaptureAdapter camera;
    FakeVisionDetector detector;
    VisionService vision(&camera, &detector, 1);
    vision.start();
    QTRY_VERIFY(vision.isAvailable());
    DelayedKwsPort kws;
    KwsConfiguration config;
    config.enabled = true;
    config.pauseTimeoutMs = 100;
    config.resumeCooldownMs = 1;
    MediaSessionCoordinator coordinator;
    coordinator.setKws(&kws, config);
    VisionAwareCallMedia media(&camera, &vision);
    TestCallPrompt prompt;
    VideoCallService call(&media, &prompt, &coordinator);
    connect(&call, &VideoCallService::callActivityChanged,
            &vision, &VisionService::setVideoCallActive);
    QSignalSpy activity(&call, &VideoCallService::callActivityChanged);

    // Repeated voice/video calls share the existing camera and defer audio.
    for (int cycle = 0; cycle < 10; ++cycle) {
        const auto mode = cycle % 2 ? VideoCallMode::Voice : VideoCallMode::Video;
        QVERIFY(call.startIncomingCall(mode).success);
        QVERIFY(media.pausedAtPrepare);
        QVERIFY(vision.isPaused());
        QCOMPARE(camera.consumerCount(), mode == VideoCallMode::Video ? 2 : 1);
        QVERIFY(!media.audio);
        QVERIFY(!prompt.playing);
        kws.acknowledge();
        QVERIFY(prompt.playing);
        QVERIFY(!media.audio);
        prompt.complete();
        QVERIFY(media.audio);
        QVERIFY(call.hangUpFromDevice().success);
        QVERIFY(!vision.isPaused());
        QVERIFY(coordinator.owner().isEmpty());
        QCOMPARE(camera.consumerCount(), 1);
        QCOMPARE(camera.startCount, 1);
        QCOMPARE(camera.stopCount, 0);
    }
    QCOMPARE(activity.count(), 20);

    // Preparation failure and KWS timeout both unpause vision and release only
    // the call's camera reference; a late pause ACK cannot resurrect the call.
    media.failPrepare = true;
    QVERIFY(!call.startOutgoingCall().success);
    QVERIFY(media.pausedAtPrepare);
    QVERIFY(!vision.isPaused());
    QVERIFY(coordinator.owner().isEmpty());
    media.failPrepare = false;
    QVERIFY(call.startIncomingCall(VideoCallMode::Video).success);
    QTRY_COMPARE(call.snapshot().state, VideoCallState::Failed);
    QVERIFY(!vision.isPaused());
    QCOMPARE(camera.consumerCount(), 1);
    kws.acknowledge();
    QVERIFY(!media.audio);
    QVERIFY(!prompt.playing);

    // A user's explicit vision pause survives a call; ending a call must not
    // turn a separately paused detector back on.
    vision.setPaused(true);
    QVERIFY(call.startOutgoingCall().success);
    QVERIFY(call.hangUpFromDevice().success);
    QVERIFY(vision.isPaused());
    vision.setPaused(false);
    camera.feed(jpegPayload("after-calls"));
    QTRY_VERIFY(detector.detectCount.load() > 0);
    coordinator.shutdown();
    vision.stop();
    QCOMPARE(camera.consumerCount(), 0);
    QCOMPARE(camera.stopCount, 1);
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

    TinyissimoYoloConfiguration missingTinyConfiguration;
    missingTinyConfiguration.modelPath = QStringLiteral(
        "definitely-missing-tinyissimo.onnx");
    TinyissimoYoloAdapter missingTinyModel(missingTinyConfiguration);
    error.clear();
    QVERIFY(!missingTinyModel.initialize(&error));
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
