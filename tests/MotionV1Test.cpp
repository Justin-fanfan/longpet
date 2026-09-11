#include "model/MediaFrameProtocol.h"
#include "platform/EspMotionProtocol.h"
#include "platform/FamilyMotionControlAdapter.h"
#include "services/MotionPorts.h"
#include "services/MotionService.h"
#include "services/AutomaticHeadTrackingService.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>
#include <QWebSocket>

namespace {
class FakeMotionPort final : public MotionPort {
public:
    using MotionPort::MotionPort;

    bool start(const QString&, int, QString*) override
    {
        started = true;
        emit transportAvailabilityChanged(available, available
            ? QStringLiteral("fake UART") : QStringLiteral("fake UART unavailable"));
        if (available && responsive) {
            emit mcuActivity();
            MotionStatusSnapshot status;
            status.uartAvailable = true;
            status.mcuOnline = true;
            status.mode = MotionControlMode::Safe;
            status.motion = ChassisMotion::Stopped;
            status.servoPulseUs = 1570;
            emit statusReceived(status);
        }
        return true;
    }
    void stop() override { started = false; available = false; }
    bool isTransportAvailable() const override { return started && available; }
    bool sendStop(QString* error) override
    { return record(QStringLiteral("STOP"), error); }
    bool sendMode(MotionControlMode mode, QString* error) override
    { return record(QStringLiteral("MODE %1").arg(motionControlModeName(mode)), error); }
    bool sendMove(ChassisMotion motion, int speed, QString* error) override
    { return record(QStringLiteral("MOVE %1 %2").arg(chassisMotionName(motion)).arg(speed), error); }
    bool sendHead(HeadMotion motion, int stepUs, QString* error) override
    {
        const QString command = motion == HeadMotion::Center
            ? QStringLiteral("HEAD CENTER")
            : QStringLiteral("HEAD %1 %2").arg(headMotionName(motion)).arg(stepUs);
        return record(command, error);
    }
    bool sendTarget(const MotionTargetFrame& target, QString* error) override
    {
        return record(QStringLiteral("TARGET %1 %2 %3")
                          .arg(target.dx).arg(target.dy).arg(target.area),
                      error);
    }
    bool requestStatus(QString* error) override
    { return record(QStringLiteral("STATUS"), error); }

    void disconnectTransport()
    {
        available = false;
        emit transportAvailabilityChanged(false, QStringLiteral("fake disconnect"));
    }

    bool record(const QString& command, QString* error)
    {
        if (!isTransportAvailable()) {
            if (error)
                *error = QStringLiteral("fake UART unavailable");
            return false;
        }
        commands.append(command);
        return true;
    }

    QStringList commands;
    bool available = true;
    bool responsive = true;
    bool started = false;
};

class FakeControlPort final : public FamilyMotionControlPort {
public:
    using FamilyMotionControlPort::FamilyMotionControlPort;

    bool start(QHostAddress, quint16 requestedPort, QString*) override
    { started = true; actualPort = requestedPort ? requestedPort : 9876; return true; }
    void stop() override { started = false; active = false; }
    quint16 port() const override { return actualPort; }
    FamilyMotionSession createSession(int refresh, int lease, int speed,
                                      int headStep, QString*) override
    {
        FamilyMotionSession session;
        if (!started || active)
            return session;
        session.sessionId = QStringLiteral("session-1");
        session.token = QStringLiteral("token-1");
        session.port = actualPort;
        session.refreshIntervalMs = refresh;
        session.leaseTimeoutMs = lease;
        session.defaultSpeed = speed;
        session.headStepUs = headStep;
        session.expiresAt = QDateTime::currentDateTimeUtc().addSecs(30);
        return session;
    }
    void acceptController(const QString& sessionId) override
    { active = true; accepted = sessionId; }
    void rejectController(const QString& sessionId, const QString& code,
                          const QString& message) override
    { rejected = sessionId; rejectionCode = code; rejectionMessage = message; }
    void terminateController(const QString& code, const QString& message) override
    { active = false; terminationCode = code; terminationMessage = message; }
    void publishStatus(const MotionStatusSnapshot& value) override
    { statuses.append(value); }
    bool hasController() const override { return active; }

    bool started = false;
    bool active = false;
    quint16 actualPort = 0;
    QString accepted;
    QString rejected;
    QString rejectionCode;
    QString rejectionMessage;
    QString terminationCode;
    QString terminationMessage;
    QList<MotionStatusSnapshot> statuses;
};

QByteArray controlFrame(const QJsonObject& object)
{
    return MediaFrameProtocol::encode(
        MediaStreamType::Control, 1,
        static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1'000,
        QJsonDocument(object).toJson(QJsonDocument::Compact));
}
}

class MotionV1Test final : public QObject {
    Q_OBJECT

private slots:
    void protocolSerializesAndParsesMcuV2();
    void targetGeometryUsesActualSourceSize();
    void automaticHeadLifecycleRejectsStaleAndYieldsToManualAndCall();
    void manualLifecycleRefreshesLeaseAndStopsSafely();
    void unavailableAndReconnectPathsDoNotCrash();
    void controlWebSocketAuthenticatesAndRejectsShift();
};

void MotionV1Test::protocolSerializesAndParsesMcuV2()
{
    QCOMPARE(EspMotionProtocol::stopCommand(), QByteArray("STOP\n"));
    QCOMPARE(EspMotionProtocol::modeCommand(MotionControlMode::Manual),
             QByteArray("MODE MANUAL\n"));
    QCOMPARE(EspMotionProtocol::moveCommand(ChassisMotion::RotateLeft, 23),
             QByteArray("MOVE ROTATE_LEFT 23\n"));
    QCOMPARE(EspMotionProtocol::headCommand(HeadMotion::Left, 20),
             QByteArray("HEAD LEFT 20\n"));
    QCOMPARE(EspMotionProtocol::headCommand(HeadMotion::Center, 0),
             QByteArray("HEAD CENTER\n"));
    QCOMPARE(EspMotionProtocol::targetCommand({-85, 12, 7400}),
             QByteArray("TARGET -85 12 7400\n"));
    QVERIFY(EspMotionProtocol::moveCommand(ChassisMotion::Stopped, 20).isEmpty());
    QVERIFY(EspMotionProtocol::moveCommand(ChassisMotion::Forward, 0).isEmpty());
    QVERIFY(EspMotionProtocol::headCommand(HeadMotion::Right, 101).isEmpty());
    QVERIFY(EspMotionProtocol::targetCommand({4097, 0, 1}).isEmpty());

    MotionStatusSnapshot status;
    QString error;
    QVERIFY2(EspMotionProtocol::parseStatusLine(
        "[STATUS] mode=MANUAL motion=FORWARD stop=NONE fault=0 target=0 servo=1570 imu=1",
        &status, &error), qPrintable(error));
    QCOMPARE(status.mode, MotionControlMode::Manual);
    QCOMPARE(status.motion, ChassisMotion::Forward);
    QCOMPARE(status.servoPulseUs, 1570);
    QVERIFY(!status.targetAvailable);
    QVERIFY(status.imuAvailable);
    QVERIFY(!EspMotionProtocol::parseStatusLine("[STATUS] broken", &status, &error));
}

void MotionV1Test::targetGeometryUsesActualSourceSize()
{
    TargetObservation observation;
    observation.present = true;
    observation.fresh = true;
    observation.status = TargetTrackingStatus::Tracking;
    observation.sourceSize = QSize(640, 480);
    observation.target.normalizedCenter = QPointF(0.25, 0.5);
    observation.target.normalizedSize = QSizeF(0.2, 0.4);
    MotionTargetFrame target;
    QString error;
    QVERIFY2(HeadTrackingGeometry::targetFromObservation(
                 observation, &target, &error), qPrintable(error));
    QCOMPARE(target.dx, -160);
    QCOMPARE(target.dy, 0);
    QCOMPARE(target.area, 24'576);

    observation.sourceSize = QSize(1024, 600);
    observation.target.normalizedCenter = QPointF(0.75, 0.25);
    observation.target.normalizedSize = QSizeF(0.1, 0.2);
    QVERIFY(HeadTrackingGeometry::targetFromObservation(
        observation, &target, &error));
    QCOMPARE(target.dx, 256);
    QCOMPARE(target.dy, -150);
    QCOMPARE(target.area, 12'288);
}

void MotionV1Test::
automaticHeadLifecycleRejectsStaleAndYieldsToManualAndCall()
{
    FakeMotionPort motion;
    FakeControlPort control;
    MotionServiceConfiguration motionConfiguration;
    motionConfiguration.mcuOfflineTimeoutMs = 5'000;
    MotionService motionService(&motion, &control, motionConfiguration);
    QString error;
    QVERIFY(motionService.start(QHostAddress::LocalHost, 9880,
                                QStringLiteral("fake"), 115200, &error));
    AutomaticHeadTrackingConfiguration configuration;
    configuration.maximumTargetAgeMs = 300;
    configuration.targetExpiryMs = 300;
    AutomaticHeadTrackingService autoHead(&motionService, configuration);
    autoHead.setVisionAvailable(true);
    QVERIFY(autoHead.setEnabled(true, &error));
    QVERIFY(motionService.isAutomaticHeadControlActive());
    QCOMPARE(motionService.status().mode, MotionControlMode::HeadOnly);
    QVERIFY(motion.commands.contains(QStringLiteral("MODE HEAD_ONLY")));

    TargetObservation observation;
    observation.present = true;
    observation.fresh = true;
    observation.status = TargetTrackingStatus::Tracking;
    observation.frameSequence = 10;
    observation.timestamp = QDateTime::currentDateTimeUtc();
    observation.sourceSize = QSize(640, 480);
    observation.target.normalizedCenter = QPointF(0.25, 0.5);
    observation.target.normalizedSize = QSizeF(0.2, 0.4);
    autoHead.handleTargetObservation(observation);
    QCOMPARE(motion.commands.constLast(),
             QStringLiteral("TARGET -160 0 24576"));
    const int targetCount = motion.commands.count(
        QStringLiteral("TARGET -160 0 24576"));
    autoHead.handleTargetObservation(observation);
    QCOMPARE(motion.commands.count(QStringLiteral("TARGET -160 0 24576")),
             targetCount);

    const int lostBeforeExpiry = motion.commands.count(
        QStringLiteral("TARGET 0 0 0"));
    QTRY_COMPARE_WITH_TIMEOUT(
        motion.commands.count(QStringLiteral("TARGET 0 0 0")),
        lostBeforeExpiry + 1, 600);
    QTest::qWait(350);
    QCOMPARE(motion.commands.count(QStringLiteral("TARGET 0 0 0")),
             lostBeforeExpiry + 1);

    observation.frameSequence = 11;
    observation.timestamp = QDateTime::currentDateTimeUtc();
    autoHead.handleTargetObservation(observation);
    QCOMPARE(motion.commands.constLast(),
             QStringLiteral("TARGET -160 0 24576"));

    observation.frameSequence = 12;
    observation.present = false;
    observation.status = TargetTrackingStatus::Lost;
    const int lostBeforeObservation = motion.commands.count(
        QStringLiteral("TARGET 0 0 0"));
    autoHead.handleTargetObservation(observation);
    QCOMPARE(motion.commands.constLast(), QStringLiteral("TARGET 0 0 0"));
    QCOMPARE(motion.commands.count(QStringLiteral("TARGET 0 0 0")),
             lostBeforeObservation + 1);

    observation.frameSequence = 13;
    observation.present = true;
    observation.status = TargetTrackingStatus::Tracking;
    observation.fresh = false;
    observation.ageMs = 900;
    const int validBeforeStale = motion.commands.count(
        QStringLiteral("TARGET -160 0 24576"));
    autoHead.handleTargetObservation(observation);
    QCOMPARE(motion.commands.count(QStringLiteral("TARGET -160 0 24576")),
             validBeforeStale);

    observation.frameSequence = 14;
    observation.fresh = true;
    observation.ageMs = 0;
    observation.timestamp = QDateTime::currentDateTimeUtc();
    autoHead.handleTargetObservation(observation);
    QCOMPARE(motion.commands.constLast(),
             QStringLiteral("TARGET -160 0 24576"));

    const FamilyMotionSession session =
        motionService.createRemoteSession(&error);
    emit control.controllerStartRequested(session.sessionId);
    QVERIFY(motionService.isManualControlActive());
    QVERIFY(!motionService.isAutomaticHeadControlActive());
    QCOMPARE(autoHead.snapshot().state,
             AutomaticHeadTrackingState::ManualOverride);
    observation.frameSequence = 15;
    autoHead.handleTargetObservation(observation);
    QVERIFY(!motion.commands.constLast().startsWith(QStringLiteral("TARGET")));

    emit control.controllerStopped(QStringLiteral("manual finished"));
    QVERIFY(!motionService.isManualControlActive());
    QVERIFY(motionService.isAutomaticHeadControlActive());
    QCOMPARE(motionService.status().mode, MotionControlMode::HeadOnly);

    autoHead.setVideoCallActive(true);
    QVERIFY(!motionService.isAutomaticHeadControlActive());
    QCOMPARE(autoHead.snapshot().state,
             AutomaticHeadTrackingState::VideoCallSuspended);
    autoHead.setVideoCallActive(false);
    QVERIFY(motionService.isAutomaticHeadControlActive());
    QCOMPARE(motionService.status().mode, MotionControlMode::HeadOnly);

    for (const QString& command : motion.commands)
        QVERIFY2(!command.startsWith(QStringLiteral("MOVE ")),
                 qPrintable(command));
}

void MotionV1Test::manualLifecycleRefreshesLeaseAndStopsSafely()
{
    FakeMotionPort motion;
    FakeControlPort control;
    MotionServiceConfiguration configuration;
    configuration.moveRefreshIntervalMs = 100;
    configuration.remoteLeaseTimeoutMs = 220;
    configuration.statusPollIntervalMs = 1'000;
    configuration.mcuOfflineTimeoutMs = 5'000;
    MotionService service(&motion, &control, configuration);
    QString error;
    QVERIFY2(service.start(QHostAddress::LocalHost, 9876,
                           QStringLiteral("fake"), 115200, &error),
             qPrintable(error));
    const FamilyMotionSession session = service.createRemoteSession(&error);
    QVERIFY(session.isValid());
    QCOMPARE(session.refreshIntervalMs, 100);
    QCOMPARE(session.leaseTimeoutMs, 220);

    emit control.controllerStartRequested(session.sessionId);
    QCOMPARE(control.accepted, session.sessionId);
    QVERIFY(motion.commands.contains(QStringLiteral("STOP")));
    QVERIFY(motion.commands.contains(QStringLiteral("MODE MANUAL")));

    const int stopBeforeHead = motion.commands.count(QStringLiteral("STOP"));
    emit control.headCommandRequested(HeadMotion::Left, 20);
    QVERIFY(motion.commands.contains(QStringLiteral("HEAD LEFT 20")));
    QCOMPARE(motion.commands.count(QStringLiteral("STOP")), stopBeforeHead);

    emit control.chassisCommandRequested(ChassisMotion::Forward, 20);
    QCOMPARE(motion.commands.constLast(), QStringLiteral("MOVE FORWARD 20"));
    const int firstMoveCount = motion.commands.count(QStringLiteral("MOVE FORWARD 20"));
    QTest::qWait(130);
    QVERIFY(motion.commands.count(QStringLiteral("MOVE FORWARD 20")) > firstMoveCount);
    emit control.chassisCommandRequested(ChassisMotion::Forward, 20);
    QTest::qWait(130);
    QVERIFY(service.status().motion == ChassisMotion::Forward);
    QTRY_COMPARE_WITH_TIMEOUT(service.status().motion, ChassisMotion::Stopped, 400);
    QVERIFY(service.status().stopReason.contains(QStringLiteral("租约超时")));

    emit control.chassisCommandRequested(ChassisMotion::Backward, 18);
    emit control.controllerStopped(QStringLiteral("window closed"));
    QCOMPARE(service.status().mode, MotionControlMode::Safe);
    QCOMPARE(service.status().motion, ChassisMotion::Stopped);
    QCOMPARE(motion.commands.constLast(), QStringLiteral("MODE SAFE"));
}

void MotionV1Test::unavailableAndReconnectPathsDoNotCrash()
{
    FakeMotionPort motion;
    motion.available = false;
    motion.responsive = false;
    FakeControlPort control;
    MotionService service(&motion, &control);
    QString error;
    QVERIFY(service.start(QHostAddress::LocalHost, 9877,
                          QStringLiteral("fake"), 115200, &error));
    const FamilyMotionSession session = service.createRemoteSession(&error);
    QVERIFY(session.isValid());
    emit control.controllerStartRequested(session.sessionId);
    QCOMPARE(control.rejectionCode, QStringLiteral("MOTION_UART_UNAVAILABLE"));

    motion.available = true;
    emit motion.transportAvailabilityChanged(true, QStringLiteral("reconnected"));
    emit motion.mcuActivity();
    MotionStatusSnapshot status;
    status.mode = MotionControlMode::Safe;
    status.motion = ChassisMotion::Stopped;
    emit motion.statusReceived(status);
    emit control.controllerStartRequested(session.sessionId);
    QCOMPARE(control.accepted, session.sessionId);
    motion.disconnectTransport();
    QCOMPARE(control.terminationCode, QStringLiteral("MOTION_UART_DISCONNECTED"));
    QCOMPARE(service.status().motion, ChassisMotion::Stopped);
}

void MotionV1Test::controlWebSocketAuthenticatesAndRejectsShift()
{
    FamilyMotionControlAdapter adapter;
    QString error;
    QVERIFY2(adapter.start(QHostAddress::LocalHost, 0, &error), qPrintable(error));
    const FamilyMotionSession session = adapter.createSession(150, 350, 20, 20, &error);
    QVERIFY(session.isValid());

    QWebSocket socket;
    QSignalSpy connected(&socket, &QWebSocket::connected);
    QSignalSpy frames(&socket, &QWebSocket::binaryMessageReceived);
    QSignalSpy starts(&adapter, &FamilyMotionControlPort::controllerStartRequested);
    QSignalSpy chassis(&adapter, &FamilyMotionControlPort::chassisCommandRequested);
    QSignalSpy stopped(&adapter, &FamilyMotionControlPort::stopRequested);
    socket.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/motion-control/v1")
                         .arg(adapter.port())));
    QTRY_COMPARE(connected.count(), 1);
    socket.sendBinaryMessage(controlFrame({
        {QStringLiteral("type"), QStringLiteral("authenticate")},
        {QStringLiteral("protocol_version"), 1},
        {QStringLiteral("session_id"), session.sessionId},
        {QStringLiteral("token"), session.token}
    }));
    QTRY_COMPARE(starts.count(), 1);
    adapter.acceptController(session.sessionId);
    QTRY_VERIFY(frames.count() >= 1);

    socket.sendBinaryMessage(controlFrame({
        {QStringLiteral("type"), QStringLiteral("chassis")},
        {QStringLiteral("direction"), QStringLiteral("FORWARD")},
        {QStringLiteral("speed"), 20}
    }));
    QTRY_COMPARE(chassis.count(), 1);
    socket.sendBinaryMessage(controlFrame({
        {QStringLiteral("type"), QStringLiteral("chassis")},
        {QStringLiteral("direction"), QStringLiteral("SHIFT_LEFT")},
        {QStringLiteral("speed"), 20}
    }));
    QTest::qWait(50);
    QCOMPARE(chassis.count(), 1);
    socket.sendBinaryMessage(controlFrame({{QStringLiteral("type"),
                                            QStringLiteral("stop")}}));
    QTRY_COMPARE(stopped.count(), 1);
    socket.close();
}

QTEST_MAIN(MotionV1Test)
#include "MotionV1Test.moc"
