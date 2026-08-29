#include "app\AppController.h"
#include "app\Application.h"
#include "app\FamilyLinkController.h"
#include "data\CareEventRepository.h"
#include "data\DatabaseManager.h"
#include "data\ReminderRepository.h"
#include "data\SettingsRepository.h"
#include "mainwindow.h"
#include "model\MediaFrameProtocol.h"
#include "pages\CarePage.h"
#include "pages\HomePage.h"
#include "pages\NetworkSetupPage.h"
#include "pages\ReminderEditPage.h"
#include "pages\ReminderPage.h"
#include "pages\SettingsPage.h"
#include "pages\VideoCallPage.h"
#include "platform\AudioVolumeAdapter.h"
#include "platform\BacklightAdapter.h"
#include "platform\FamilyLinkHttpAdapter.h"
#include "platform\NetworkStatusAdapter.h"
#include "platform\NetworkManagerAdapter.h"
#include "platform\PowerStatusAdapter.h"
#include "services\CareService.h"
#include "services\FamilyLinkService.h"
#include "services\ReminderService.h"
#include "services\NetworkService.h"
#include "services\SettingsService.h"
#include "services\SystemService.h"
#include "services\VideoCallService.h"
#include "services\VideoCallPorts.h"
#include "widgets\PetFaceWidget.h"
#include "widgets\VisualComponents.h"
#include "widgets\VisualTokens.h"

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QSvgRenderer>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

class V02Test final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void resourcesAreEmbedded();
    void databaseCreatesVersionedSchema();
    void applicationCompositionRootInitializes();
    void networkStatusAdapterMapsAndDegrades();
    void networkConfigurationParsesValidatesAndEmits();
    void hardwareAdaptersMapAndDegrade();
    void reminderCrudPersistsAndRejectsStaleRevision();
    void reminderSchedulerDoesNotRedeliverToday();
    void careAndSettingsArePersistentServices();
    void familyLinkApiReadsAndWritesServiceData();
    void videoCallStateAndApiTransition();
    void mediaFrameProtocolAndIncomingCallLifecycle();
    void pagesExposeSemanticSignalsAndModels();
    void applicationControllerOwnsNavigation();
    void statusBarRemains64AndShowsSystemInput();
    void controlTimeoutWorksAcrossBusinessPages();
    void renderV02Pages();
};

namespace {
class FakeVideoCallMediaPort final : public VideoCallMediaPort {
public:
    using VideoCallMediaPort::VideoCallMediaPort;

    quint16 port() const override { return 8'788; }
    bool prepare(const VideoCallSnapshot& snapshot, bool audioEnabled,
                 QString*) override
    {
        preparedSnapshot = snapshot;
        prepared = true;
        audio = audioEnabled;
        return prepareSucceeds;
    }
    void enableAudio() override { audio = true; }
    void stop() override { ++stopCount; prepared = false; audio = false; }
    void triggerReady() { emit mediaReady(); }

    VideoCallSnapshot preparedSnapshot;
    bool prepareSucceeds = true;
    bool prepared = false;
    bool audio = false;
    int stopCount = 0;
};

class FakeCallPromptPlayer final : public CallPromptPlayerPort {
public:
    using CallPromptPlayerPort::CallPromptPlayerPort;

    bool play(VideoCallMode mode, QString*) override
    {
        playedMode = mode;
        playing = playSucceeds;
        return playSucceeds;
    }
    void stop() override { playing = false; ++stopCount; }
    void complete() { playing = false; emit finished(); }

    VideoCallMode playedMode = VideoCallMode::Video;
    bool playSucceeds = true;
    bool playing = false;
    int stopCount = 0;
};

struct ServiceFixture {
    QTemporaryDir directory;
    DatabaseManager database;
    std::unique_ptr<ReminderRepository> reminders;
    std::unique_ptr<CareEventRepository> careEvents;
    std::unique_ptr<SettingsRepository> settingsRepository;
    std::unique_ptr<ReminderService> reminderService;
    std::unique_ptr<CareService> careService;
    std::unique_ptr<SettingsService> settingsService;

    bool open(QString* error = nullptr)
    {
        if (!directory.isValid())
            return false;
        if (!database.open(QDir(directory.path()).filePath(QStringLiteral("test.db")), error))
            return false;
        reminders = std::make_unique<ReminderRepository>(database.database());
        careEvents = std::make_unique<CareEventRepository>(database.database());
        settingsRepository = std::make_unique<SettingsRepository>(database.database());
        reminderService = std::make_unique<ReminderService>(reminders.get());
        careService = std::make_unique<CareService>(careEvents.get(), reminderService.get());
        settingsService = std::make_unique<SettingsService>(settingsRepository.get());
        return true;
    }
};

ReminderDraft medicineDraft()
{
    ReminderDraft draft;
    draft.type = ReminderType::Medicine;
    draft.title = QStringLiteral("晚饭后吃药");
    draft.timeOfDay = QTime(20, 0);
    draft.repeatRule = ReminderRepeatRule::Daily;
    return draft;
}

struct HttpTestResult {
    int statusCode = 0;
    QJsonObject body;
    QString error;
};

HttpTestResult requestJson(QNetworkAccessManager* manager, const QUrl& url,
                           const QByteArray& method = QByteArrayLiteral("GET"),
                           const QByteArray& bearerToken = {},
                           const QByteArray& body = {})
{
    QNetworkRequest request(url);
    request.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json"));
    if (!bearerToken.isEmpty())
        request.setRawHeader(QByteArrayLiteral("Authorization"),
                             QByteArrayLiteral("Bearer ") + bearerToken);
    if (!body.isEmpty())
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QStringLiteral("application/json"));
    QNetworkReply* reply = manager->sendCustomRequest(request, method, body);
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&] {
        reply->abort();
        loop.quit();
    });
    timeout.start(2'000);
    if (!reply->isFinished())
        loop.exec();

    HttpTestResult result;
    result.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray payload = reply->readAll();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        result.error = parseError.errorString();
    else
        result.body = document.object();
    reply->deleteLater();
    return result;
}
}

void V02Test::initTestCase()
{
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.0"));
    qRegisterMetaType<WifiNetwork>();
    qRegisterMetaType<QList<WifiNetwork>>();
    qRegisterMetaType<VideoCallSnapshot>();
    QFile styleFile(QStringLiteral(":/styles/app.qss"));
    QVERIFY(styleFile.open(QIODevice::ReadOnly | QIODevice::Text));
    qApp->setStyleSheet(QString::fromUtf8(styleFile.readAll()));
}

void V02Test::resourcesAreEmbedded()
{
    const QStringList resources {
        QStringLiteral(":/styles/app.qss"), QStringLiteral(":/icons/back.svg"),
        QStringLiteral(":/icons/microphone-dark.svg"), QStringLiteral(":/icons/care.svg"),
        QStringLiteral(":/icons/reminder.svg"), QStringLiteral(":/icons/settings.svg"),
        QStringLiteral(":/icons/weather-sunny.svg"), QStringLiteral(":/icons/wifi.svg"),
        QStringLiteral(":/icons/battery.svg"), QStringLiteral(":/icons/pill.svg"),
        QStringLiteral(":/icons/water.svg"), QStringLiteral(":/icons/activity.svg"),
        QStringLiteral(":/icons/check.svg"), QStringLiteral(":/icons/check-dark.svg"),
        QStringLiteral(":/icons/clock.svg"), QStringLiteral(":/icons/alert.svg"),
        QStringLiteral(":/icons/phone-dark.svg"), QStringLiteral(":/icons/volume.svg"),
        QStringLiteral(":/icons/brightness.svg"), QStringLiteral(":/icons/network.svg"),
        QStringLiteral(":/icons/family.svg"), QStringLiteral(":/icons/pet.svg"),
        QStringLiteral(":/icons/info.svg"), QStringLiteral(":/icons/home.svg"),
        QStringLiteral(":/icons/plus-dark.svg"), QStringLiteral(":/icons/chevron.svg"),
        QStringLiteral(":/icons/palette.svg")
    };
    for (const QString& path : resources)
        QVERIFY2(QFile::exists(path), qPrintable(path));
    for (const QString& path : resources.mid(1))
        QVERIFY2(QSvgRenderer(path).isValid(), qPrintable(path));
    QVERIFY(QFile::exists(QStringLiteral(":/sounds/zh_video_call.wav")));
    QVERIFY(QFile::exists(QStringLiteral(":/sounds/zh_voice_call.wav")));
}

void V02Test::databaseCreatesVersionedSchema()
{
    ServiceFixture fixture;
    QString error;
    QVERIFY2(fixture.open(&error), qPrintable(error));
    QCOMPARE(fixture.database.schemaVersion(), 1);

    const QStringList expected {
        QStringLiteral("schema_meta"), QStringLiteral("reminders"),
        QStringLiteral("reminder_events"), QStringLiteral("care_events"),
        QStringLiteral("settings")
    };
    QSqlQuery query(fixture.database.database());
    QVERIFY(query.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table'")));
    QStringList names;
    while (query.next())
        names.append(query.value(0).toString());
    for (const QString& table : expected)
        QVERIFY2(names.contains(table), qPrintable(table));
}

void V02Test::applicationCompositionRootInitializes()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = QDir(directory.path()).filePath(QStringLiteral("app.db"));
    QVERIFY(qputenv("LONGPET_DATABASE_PATH", databasePath.toUtf8()));
    QVERIFY(qputenv("LONGPET_FAMILY_LINK_PORT", QByteArrayLiteral("0")));
    QVERIFY(qputenv("LONGPET_FAMILY_LINK_ADDRESS", QByteArrayLiteral("127.0.0.1")));
    {
        Application application;
        QString error;
        QVERIFY2(application.initialize(&error), qPrintable(error));
        QVERIFY(application.window());
        QCOMPARE(application.databasePath(), databasePath);
    }
    qunsetenv("LONGPET_DATABASE_PATH");
    qunsetenv("LONGPET_FAMILY_LINK_PORT");
    qunsetenv("LONGPET_FAMILY_LINK_ADDRESS");
}

void V02Test::networkStatusAdapterMapsAndDegrades()
{
    using Reachability = QNetworkInformation::Reachability;
    using Transport = QNetworkInformation::TransportMedium;

    const NetworkStatusSnapshot unknown = NetworkStatusAdapter::mapState(
        Reachability::Unknown, Transport::WiFi);
    QVERIFY(!unknown.known);
    QVERIFY(!unknown.internetAvailable);
    QCOMPARE(unknown.summary, QStringLiteral("Wi-Fi · 状态未知"));

    const NetworkStatusSnapshot disconnected = NetworkStatusAdapter::mapState(
        Reachability::Disconnected, Transport::Unknown);
    QVERIFY(disconnected.known);
    QVERIFY(!disconnected.internetAvailable);
    QCOMPARE(disconnected.summary, QStringLiteral("未连接"));

    const NetworkStatusSnapshot local = NetworkStatusAdapter::mapState(
        Reachability::Local, Transport::WiFi);
    QVERIFY(local.known);
    QVERIFY(!local.internetAvailable);
    QCOMPARE(local.summary, QStringLiteral("Wi-Fi · 无互联网"));

    const NetworkStatusSnapshot site = NetworkStatusAdapter::mapState(
        Reachability::Site, Transport::Ethernet);
    QVERIFY(site.known);
    QVERIFY(!site.internetAvailable);
    QCOMPARE(site.summary, QStringLiteral("以太网 · 无互联网"));

    const NetworkStatusSnapshot online = NetworkStatusAdapter::mapState(
        Reachability::Online, Transport::WiFi);
    QVERIFY(online.known);
    QVERIFY(online.internetAvailable);
    QCOMPARE(online.summary, QStringLiteral("Wi-Fi · 已联网"));

    QCOMPARE(NetworkStatusAdapter::mapState(
        Reachability::Online, Transport::Cellular).summary,
        QStringLiteral("蜂窝网络 · 已联网"));
    QCOMPARE(NetworkStatusAdapter::mapState(
        Reachability::Online, Transport::Bluetooth).summary,
        QStringLiteral("蓝牙 · 已联网"));

    const NetworkStatusSnapshot captivePortal = NetworkStatusAdapter::mapState(
        Reachability::Online, Transport::Ethernet, true);
    QVERIFY(captivePortal.known);
    QVERIFY(!captivePortal.internetAvailable);
    QCOMPARE(captivePortal.summary, QStringLiteral("以太网 · 需认证"));

    NetworkStatusAdapter unavailable(
        NetworkStatusAdapter::BackendProvider([] { return nullptr; }));
    SystemService systemService;
    connect(&unavailable, &NetworkStatusAdapter::networkStateChanged,
            &systemService, &SystemService::setNetworkState);
    QSignalSpy statusSpy(&unavailable,
                         &NetworkStatusAdapter::networkStateChanged);
    QVERIFY(!unavailable.start());
    QCOMPARE(statusSpy.count(), 1);
    const QList<QVariant> arguments = statusSpy.takeFirst();
    QVERIFY(!arguments.at(0).toBool());
    QVERIFY(!arguments.at(1).toBool());
    QCOMPARE(arguments.at(2).toString(), QStringLiteral("网络状态未知"));
    QVERIFY(!systemService.status().networkKnown);
    QVERIFY(!systemService.status().networkAvailable);
    QCOMPARE(systemService.status().networkSummary,
             QStringLiteral("网络状态未知"));
    QCOMPARE(systemService.deviceSummary().networkSummary,
             QStringLiteral("网络状态未知"));
}

void V02Test::networkConfigurationParsesValidatesAndEmits()
{
    const QByteArray scanOutput = QByteArrayLiteral(
        "*:Home\\:Main:88:WPA2 WPA3\n"
        ":Guest:42:--\n"
        ":Corp:60:WPA2 802.1X\n"
        ":Home\\:Main:70:WPA2\n");
    const QList<WifiNetwork> parsed =
        NetworkManagerAdapter::parseScanOutput(scanOutput);
    QCOMPARE(parsed.size(), 3);
    QCOMPARE(parsed.at(0).ssid, QStringLiteral("Home:Main"));
    QCOMPARE(parsed.at(0).signalStrength, 88);
    QVERIFY(parsed.at(0).connected);
    QVERIFY(parsed.at(0).requiresPassword);
    QCOMPARE(parsed.at(1).ssid, QStringLiteral("Corp"));
    QVERIFY(!parsed.at(1).supported);
    QCOMPARE(parsed.at(2).ssid, QStringLiteral("Guest"));
    QVERIFY(!parsed.at(2).requiresPassword);

    NetworkManagerAdapter adapter(QStringLiteral("missing-nmcli"),
                                  QStringLiteral("wlan0"));
    NetworkService service(&adapter);
    QSignalSpy validationSpy(&service, &NetworkService::connectionFailed);
    WifiNetwork secured;
    secured.ssid = QStringLiteral("Test Wi-Fi");
    secured.security = QStringLiteral("WPA2");
    secured.requiresPassword = true;
    service.connectToNetwork(secured, {});
    QCOMPARE(validationSpy.count(), 1);
    QCOMPARE(validationSpy.takeFirst().at(1).toString(),
             QStringLiteral("请输入网络密码"));

    NetworkSetupPage page;
    QSignalSpy connectSpy(&page, &NetworkSetupPage::connectionRequested);
    page.setNetworks({secured});
    auto* list = page.findChild<QListWidget*>(QStringLiteral("wifiNetworkList"));
    QVERIFY(list);
    list->setCurrentRow(0);
    auto* password = page.findChild<QLineEdit*>(QStringLiteral("wifiPasswordEdit"));
    QVERIFY(password);
    password->setText(QStringLiteral("secret123"));
    QTest::mouseClick(page.findChild<QPushButton*>(QStringLiteral("wifiConnectButton")),
                      Qt::LeftButton);
    QCOMPARE(connectSpy.count(), 1);
    const QList<QVariant> request = connectSpy.takeFirst();
    QCOMPARE(qvariant_cast<WifiNetwork>(request.at(0)).ssid,
             QStringLiteral("Test Wi-Fi"));
    QCOMPARE(request.at(1).toString(), QStringLiteral("secret123"));
    QVERIFY(password->text().isEmpty());
}

void V02Test::hardwareAdaptersMapAndDegrade()
{
    QCOMPARE(AudioVolumeAdapter::scalePercentToRange(0, 0, 192), 0L);
    QCOMPARE(AudioVolumeAdapter::scalePercentToRange(50, 0, 192), 96L);
    QCOMPARE(AudioVolumeAdapter::scalePercentToRange(100, 0, 192), 192L);
    QCOMPARE(AudioVolumeAdapter::scalePercentToRange(120, 10, 20), 20L);

    QCOMPARE(BacklightAdapter::percentToRaw(0, 1), 0);
    QCOMPARE(BacklightAdapter::percentToRaw(1, 1), 1);
    QCOMPARE(BacklightAdapter::percentToRaw(50, 255), 128);
    QCOMPARE(BacklightAdapter::rawToPercent(128, 255), 50);

    QTemporaryDir backlightRoot;
    QVERIFY(backlightRoot.isValid());
    const QString devicePath = QDir(backlightRoot.path()).filePath(
        QStringLiteral("panel"));
    QVERIFY(QDir().mkpath(devicePath));
    auto writeValue = [&devicePath](const QString& name, const QByteArray& value) {
        QFile file(QDir(devicePath).filePath(name));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        return file.write(value) == value.size();
    };
    QVERIFY(writeValue(QStringLiteral("max_brightness"), QByteArrayLiteral("1")));
    QVERIFY(writeValue(QStringLiteral("brightness"), QByteArrayLiteral("0")));
    QVERIFY(writeValue(QStringLiteral("actual_brightness"), QByteArrayLiteral("1")));

    BacklightAdapter backlight(backlightRoot.path());
    QSignalSpy backlightStateSpy(&backlight,
        &BacklightAdapter::controlStateChanged);
    QSignalSpy brightnessSpy(&backlight,
        &BacklightAdapter::brightnessApplied);
    QVERIFY(backlight.start());
    QCOMPARE(backlight.brightnessLevels(), 2);
    QCOMPARE(backlightStateSpy.count(), 1);
    backlight.applyBrightness(72);
    QCOMPARE(brightnessSpy.count(), 1);
    QCOMPARE(brightnessSpy.takeFirst().at(0).toInt(), 100);
    QFile brightnessFile(QDir(devicePath).filePath(QStringLiteral("brightness")));
    QVERIFY(brightnessFile.open(QIODevice::ReadOnly));
    QCOMPARE(brightnessFile.readAll(), QByteArrayLiteral("1"));

    QTemporaryDir powerRoot;
    QVERIFY(powerRoot.isValid());
    const QString batteryPath = QDir(powerRoot.path()).filePath(
        QStringLiteral("BAT0"));
    QVERIFY(QDir().mkpath(batteryPath));
    auto writeBatteryValue = [&batteryPath](const QString& name,
                                             const QByteArray& value) {
        QFile file(QDir(batteryPath).filePath(name));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        return file.write(value) == value.size();
    };
    QVERIFY(writeBatteryValue(QStringLiteral("type"), QByteArrayLiteral("Battery")));
    QVERIFY(writeBatteryValue(QStringLiteral("capacity"), QByteArrayLiteral("73")));
    QVERIFY(writeBatteryValue(QStringLiteral("status"), QByteArrayLiteral("Charging")));
    PowerStatusAdapter power(powerRoot.path(), 60'000);
    QSignalSpy batterySpy(&power, &PowerStatusAdapter::batteryPercentChanged);
    QVERIFY(power.start());
    QCOMPARE(batterySpy.count(), 1);
    QCOMPARE(batterySpy.takeFirst().at(0).toInt(), 73);

    PowerStatusAdapter unavailablePower(
        QDir(powerRoot.path()).filePath(QStringLiteral("missing")), 60'000);
    QSignalSpy unavailableBatterySpy(
        &unavailablePower, &PowerStatusAdapter::batteryPercentChanged);
    QVERIFY(!unavailablePower.start());
    QCOMPARE(unavailableBatterySpy.count(), 1);
    QCOMPARE(unavailableBatterySpy.takeFirst().at(0).toInt(), -1);
}

void V02Test::reminderCrudPersistsAndRejectsStaleRevision()
{
    ServiceFixture fixture;
    QString error;
    QVERIFY2(fixture.open(&error), qPrintable(error));

    ReminderDraft draft = medicineDraft();
    ServiceResult created = fixture.reminderService->save(draft);
    QVERIFY2(created.success, qPrintable(created.error));
    QCOMPARE(fixture.reminderService->reminders().size(), 1);

    bool found = false;
    Reminder stored = fixture.reminderService->reminder(created.id, &found, &error);
    QVERIFY(found);
    QCOMPARE(stored.title, draft.title);
    QCOMPARE(stored.revision, 1);

    draft.id = stored.id;
    draft.expectedRevision = stored.revision;
    draft.title = QStringLiteral("睡前吃药");
    ServiceResult updated = fixture.reminderService->save(draft);
    QVERIFY2(updated.success, qPrintable(updated.error));
    QCOMPARE(fixture.reminderService->reminder(stored.id, &found).revision, 2);

    draft.title = QStringLiteral("旧版本覆盖");
    ServiceResult stale = fixture.reminderService->save(draft);
    QVERIFY(!stale.success);
    QVERIFY(stale.error.contains(QStringLiteral("刷新")));

    QVERIFY(fixture.reminderService->markCompleted(stored.id).success);
    QCOMPARE(fixture.reminderService->reminders().first().status,
             ReminderOccurrenceStatus::Completed);
    QVERIFY(fixture.reminderService->remove(stored.id).success);
    QVERIFY(fixture.reminderService->reminders().isEmpty());
}

void V02Test::reminderSchedulerDoesNotRedeliverToday()
{
    ServiceFixture fixture;
    QString error;
    QVERIFY2(fixture.open(&error), qPrintable(error));
    ReminderDraft draft = medicineDraft();
    draft.timeOfDay = QTime::currentTime();
    QVERIFY(fixture.reminderService->save(draft).success);

    QSignalSpy triggerSpy(fixture.reminderService.get(),
                          &ReminderService::reminderTriggered);
    fixture.reminderService->start();
    QCOMPARE(triggerSpy.count(), 1);
    QCOMPARE(fixture.reminderService->reminders().first().status,
             ReminderOccurrenceStatus::Pending);
    fixture.reminderService->stop();
    fixture.reminderService->start();
    QCOMPARE(triggerSpy.count(), 1);
    QVERIFY(fixture.reminders->hasEventForDate(
        fixture.reminderService->reminders().first().id, QDate::currentDate(), &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));
}

void V02Test::careAndSettingsArePersistentServices()
{
    ServiceFixture fixture;
    QString error;
    QVERIFY2(fixture.open(&error), qPrintable(error));
    QVERIFY(fixture.reminderService->save(medicineDraft()).success);
    QVERIFY(fixture.careService->recordWater().success);
    QVERIFY(fixture.careService->recordWater().success);
    QVERIFY(fixture.careService->recordActivityMinutes(12, QStringLiteral("imu")).success);
    QVERIFY(fixture.careService->recordInteraction(3, QStringLiteral("camera")).success);
    const CareSummary summary = fixture.careService->todaySummary(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(summary.waterCompleted, 2);
    QCOMPARE(summary.medicineTotal, 1);
    QCOMPARE(summary.activityMinutes, 12);
    QCOMPARE(summary.interactionCount, 3);

    QVERIFY(fixture.settingsService->setVolume(35, &error));
    QVERIFY(fixture.settingsService->setBrightness(80, &error));
    QVERIFY(fixture.settingsService->setPetStyle(QStringLiteral("活泼陪伴"), &error));
    const UserSettings settings = fixture.settingsService->settings(&error);
    QCOMPARE(settings.volume, 35);
    QCOMPARE(settings.brightness, 80);
    QCOMPARE(settings.petStyle, QStringLiteral("活泼陪伴"));
    QVERIFY(!fixture.settingsService->setVolume(101, &error));
}

void V02Test::familyLinkApiReadsAndWritesServiceData()
{
    ServiceFixture fixture;
    QString error;
    QVERIFY2(fixture.open(&error), qPrintable(error));
    QVERIFY(fixture.reminderService->save(medicineDraft()).success);
    QVERIFY(fixture.careService->recordWater().success);
    QVERIFY(fixture.settingsService->setVolume(47, &error));
    QVERIFY(fixture.settingsService->setPetStyle(QStringLiteral("活泼陪伴"), &error));

    SystemService systemService;
    systemService.setNetworkState(true, true, QStringLiteral("Wi-Fi · 已联网"));
    systemService.setAudioControlState(true, QStringLiteral("USB 声卡可用"));
    systemService.setBacklightControlState(false, 0, QStringLiteral("未检测到可调背光"));
    systemService.setPowerSummary(QStringLiteral("外接电源"));

    VideoCallService videoCallService;
    FamilyLinkService service(fixture.reminderService.get(), fixture.careService.get(),
                              fixture.settingsService.get(), &systemService,
                              &videoCallService);
    FamilyLinkHttpAdapter httpAdapter;
    const QByteArray token = QByteArrayLiteral("test-family-token");
    FamilyLinkController controller(&service, &httpAdapter, token);
    QVERIFY2(controller.start(0, QHostAddress(QHostAddress::LocalHost), &error),
             qPrintable(error));
    QVERIFY(controller.port() > 0);

    const QString baseUrl = QStringLiteral("http://127.0.0.1:%1/api/v1/")
        .arg(controller.port());
    QNetworkAccessManager manager;

    const HttpTestResult unauthorized = requestJson(
        &manager, QUrl(baseUrl + QStringLiteral("status")));
    QCOMPARE(unauthorized.statusCode, 401);
    QCOMPARE(unauthorized.body.value(QStringLiteral("error")).toObject()
                 .value(QStringLiteral("code")).toString(),
             QStringLiteral("AUTHENTICATION_REQUIRED"));

    const HttpTestResult status = requestJson(
        &manager, QUrl(baseUrl + QStringLiteral("status")), QByteArrayLiteral("GET"), token);
    QCOMPARE(status.statusCode, 200);
    QVERIFY2(status.error.isEmpty(), qPrintable(status.error));
    QVERIFY(status.body.value(QStringLiteral("device")).toObject()
                .value(QStringLiteral("online")).toBool());
    QVERIFY(status.body.value(QStringLiteral("system")).toObject()
                .value(QStringLiteral("networkAvailable")).toBool());
    QVERIFY(status.body.value(QStringLiteral("system")).toObject()
                .value(QStringLiteral("currentDateTime")).toString().endsWith(QLatin1Char('Z')));
    QCOMPARE(status.body.value(QStringLiteral("care")).toObject()
                 .value(QStringLiteral("waterCompleted")).toInt(), 1);
    QVERIFY(status.body.value(QStringLiteral("capabilities")).toObject()
                .value(QStringLiteral("settingsWrite")).toBool());
    QVERIFY(status.body.value(QStringLiteral("capabilities")).toObject()
                .value(QStringLiteral("remindersWrite")).toBool());
    QVERIFY(status.body.value(QStringLiteral("capabilities")).toObject()
                .value(QStringLiteral("videoCallSignaling")).toBool());

    const VideoCallResult startedCall = videoCallService.startOutgoingCall();
    QVERIFY(startedCall.success);
    const HttpTestResult ringingCall = requestJson(
        &manager, QUrl(baseUrl + QStringLiteral("video-call")),
        QByteArrayLiteral("GET"), token);
    QCOMPARE(ringingCall.statusCode, 200);
    QCOMPARE(ringingCall.body.value(QStringLiteral("state")).toString(),
             QStringLiteral("outgoing_ringing"));
    QCOMPARE(ringingCall.body.value(QStringLiteral("callId")).toString(),
             startedCall.snapshot.callId);

    const QByteArray acceptBody = QJsonDocument(QJsonObject {
        {QStringLiteral("callId"), startedCall.snapshot.callId},
        {QStringLiteral("action"), QStringLiteral("accept")},
        {QStringLiteral("expectedRevision"), startedCall.snapshot.revision}
    }).toJson(QJsonDocument::Compact);
    const HttpTestResult acceptedCall = requestJson(
        &manager, QUrl(baseUrl + QStringLiteral("video-call/actions")),
        QByteArrayLiteral("POST"), token, acceptBody);
    QCOMPARE(acceptedCall.statusCode, 200);
    QCOMPARE(acceptedCall.body.value(QStringLiteral("state")).toString(),
             QStringLiteral("connected"));

    const HttpTestResult staleCallAction = requestJson(
        &manager, QUrl(baseUrl + QStringLiteral("video-call/actions")),
        QByteArrayLiteral("POST"), token, acceptBody);
    QCOMPARE(staleCallAction.statusCode, 409);
    QCOMPARE(staleCallAction.body.value(QStringLiteral("error")).toObject()
                 .value(QStringLiteral("code")).toString(),
             QStringLiteral("REVISION_CONFLICT"));

    const QByteArray hangUpBody = QJsonDocument(QJsonObject {
        {QStringLiteral("callId"), startedCall.snapshot.callId},
        {QStringLiteral("action"), QStringLiteral("hangup")},
        {QStringLiteral("expectedRevision"),
         acceptedCall.body.value(QStringLiteral("revision")).toInt()}
    }).toJson(QJsonDocument::Compact);
    const HttpTestResult endedCall = requestJson(
        &manager, QUrl(baseUrl + QStringLiteral("video-call/actions")),
        QByteArrayLiteral("POST"), token, hangUpBody);
    QCOMPARE(endedCall.statusCode, 200);
    QCOMPARE(endedCall.body.value(QStringLiteral("state")).toString(),
             QStringLiteral("ended"));

    const QByteArray startVoiceBody = QJsonDocument(QJsonObject {
        {QStringLiteral("mode"), QStringLiteral("voice")}
    }).toJson(QJsonDocument::Compact);
    const HttpTestResult incomingVoice = requestJson(
        &manager, QUrl(baseUrl + QStringLiteral("video-call")),
        QByteArrayLiteral("POST"), token, startVoiceBody);
    QCOMPARE(incomingVoice.statusCode, 201);
    QCOMPARE(incomingVoice.body.value(QStringLiteral("mode")).toString(),
             QStringLiteral("voice"));
    QCOMPARE(incomingVoice.body.value(QStringLiteral("direction")).toString(),
             QStringLiteral("family_to_device"));
    const HttpTestResult busyCall = requestJson(
        &manager, QUrl(baseUrl + QStringLiteral("video-call")),
        QByteArrayLiteral("POST"), token, startVoiceBody);
    QCOMPARE(busyCall.statusCode, 409);
    QCOMPARE(busyCall.body.value(QStringLiteral("error")).toObject()
                 .value(QStringLiteral("code")).toString(),
             QStringLiteral("DEVICE_BUSY"));

    const QByteArray endIncomingBody = QJsonDocument(QJsonObject {
        {QStringLiteral("callId"), incomingVoice.body.value(QStringLiteral("callId"))},
        {QStringLiteral("action"), QStringLiteral("hangup")},
        {QStringLiteral("expectedRevision"),
         incomingVoice.body.value(QStringLiteral("revision"))}
    }).toJson(QJsonDocument::Compact);
    const HttpTestResult endedIncoming = requestJson(
        &manager, QUrl(baseUrl + QStringLiteral("video-call/actions")),
        QByteArrayLiteral("POST"), token, endIncomingBody);
    QCOMPARE(endedIncoming.statusCode, 200);

    const HttpTestResult settings = requestJson(
        &manager, QUrl(baseUrl + QStringLiteral("settings")), QByteArrayLiteral("GET"), token);
    QCOMPARE(settings.statusCode, 200);
    QVERIFY2(settings.error.isEmpty(), qPrintable(settings.error));
    QCOMPARE(settings.body.value(QStringLiteral("volume")).toInt(), 47);
    QCOMPARE(settings.body.value(QStringLiteral("petStyle")).toString(),
             QStringLiteral("活泼陪伴"));
    QVERIFY(settings.body.value(QStringLiteral("remoteWritable")).toBool());
    const int settingsRevision = settings.body.value(QStringLiteral("revision")).toInt();
    QCOMPARE(settingsRevision, 2);

    QSignalSpy applySpy(fixture.settingsService.get(),
                        &SettingsService::settingApplyRequested);
    const QByteArray settingsPatch = QJsonDocument(QJsonObject {
        {QStringLiteral("volume"), 52},
        {QStringLiteral("expectedRevision"), settingsRevision}
    }).toJson(QJsonDocument::Compact);
    const HttpTestResult updatedSettings = requestJson(
        &manager, QUrl(baseUrl + QStringLiteral("settings")),
        QByteArrayLiteral("PATCH"), token, settingsPatch);
    QCOMPARE(updatedSettings.statusCode, 200);
    QCOMPARE(updatedSettings.body.value(QStringLiteral("volume")).toInt(), 52);
    QCOMPARE(updatedSettings.body.value(QStringLiteral("revision")).toInt(),
             settingsRevision + 1);
    QCOMPARE(fixture.settingsService->settings().volume, 52);
    QCOMPARE(applySpy.count(), 1);

    const HttpTestResult staleSettings = requestJson(
        &manager, QUrl(baseUrl + QStringLiteral("settings")),
        QByteArrayLiteral("PATCH"), token, settingsPatch);
    QCOMPARE(staleSettings.statusCode, 409);
    QCOMPARE(staleSettings.body.value(QStringLiteral("error")).toObject()
                 .value(QStringLiteral("code")).toString(),
             QStringLiteral("REVISION_CONFLICT"));
    QCOMPARE(staleSettings.body.value(QStringLiteral("error")).toObject()
                 .value(QStringLiteral("details")).toObject()
                 .value(QStringLiteral("currentRevision")).toInt(), settingsRevision + 1);

    const QByteArray unavailableBrightnessPatch = QJsonDocument(QJsonObject {
        {QStringLiteral("brightness"), 40},
        {QStringLiteral("expectedRevision"), settingsRevision + 1}
    }).toJson(QJsonDocument::Compact);
    const HttpTestResult unavailableBrightness = requestJson(
        &manager, QUrl(baseUrl + QStringLiteral("settings")),
        QByteArrayLiteral("PATCH"), token, unavailableBrightnessPatch);
    QCOMPARE(unavailableBrightness.statusCode, 503);
    QCOMPARE(unavailableBrightness.body.value(QStringLiteral("error")).toObject()
                 .value(QStringLiteral("code")).toString(),
             QStringLiteral("CAPABILITY_UNAVAILABLE"));
    QCOMPARE(fixture.settingsService->revision(), settingsRevision + 1);

    const HttpTestResult reminders = requestJson(
        &manager, QUrl(baseUrl + QStringLiteral("reminders")), QByteArrayLiteral("GET"), token);
    QCOMPARE(reminders.statusCode, 200);
    QVERIFY2(reminders.error.isEmpty(), qPrintable(reminders.error));
    QCOMPARE(reminders.body.value(QStringLiteral("items")).toArray().size(), 1);

    const QByteArray createBody = QJsonDocument(QJsonObject {
        {QStringLiteral("type"), QStringLiteral("water")},
        {QStringLiteral("title"), QStringLiteral("上午喝水")},
        {QStringLiteral("timeOfDay"), QStringLiteral("10:15")},
        {QStringLiteral("scheduledDate"), QDate::currentDate().toString(Qt::ISODate)},
        {QStringLiteral("repeatRule"), QStringLiteral("daily")},
        {QStringLiteral("enabled"), true}
    }).toJson(QJsonDocument::Compact);
    const HttpTestResult created = requestJson(
        &manager, QUrl(baseUrl + QStringLiteral("reminders")),
        QByteArrayLiteral("POST"), token, createBody);
    QCOMPARE(created.statusCode, 201);
    const ReminderId createdId = created.body.value(QStringLiteral("id")).toInteger();
    QVERIFY(createdId > 0);
    QCOMPARE(created.body.value(QStringLiteral("revision")).toInt(), 1);

    const QByteArray updateBody = QJsonDocument(QJsonObject {
        {QStringLiteral("type"), QStringLiteral("water")},
        {QStringLiteral("title"), QStringLiteral("上午补水")},
        {QStringLiteral("timeOfDay"), QStringLiteral("10:20")},
        {QStringLiteral("scheduledDate"), QDate::currentDate().toString(Qt::ISODate)},
        {QStringLiteral("repeatRule"), QStringLiteral("daily")},
        {QStringLiteral("enabled"), true},
        {QStringLiteral("expectedRevision"), 1}
    }).toJson(QJsonDocument::Compact);
    const QUrl reminderUrl(baseUrl + QStringLiteral("reminders/%1").arg(createdId));
    const HttpTestResult updated = requestJson(
        &manager, reminderUrl, QByteArrayLiteral("PUT"), token, updateBody);
    QCOMPARE(updated.statusCode, 200);
    QCOMPARE(updated.body.value(QStringLiteral("title")).toString(),
             QStringLiteral("上午补水"));
    QCOMPARE(updated.body.value(QStringLiteral("revision")).toInt(), 2);

    const HttpTestResult staleReminder = requestJson(
        &manager, reminderUrl, QByteArrayLiteral("PUT"), token, updateBody);
    QCOMPARE(staleReminder.statusCode, 409);
    QCOMPARE(staleReminder.body.value(QStringLiteral("error")).toObject()
                 .value(QStringLiteral("code")).toString(),
             QStringLiteral("REVISION_CONFLICT"));

    const HttpTestResult deleted = requestJson(
        &manager, QUrl(reminderUrl.toString() + QStringLiteral("?expectedRevision=2")),
        QByteArrayLiteral("DELETE"), token);
    QCOMPARE(deleted.statusCode, 200);
    QVERIFY(deleted.body.value(QStringLiteral("deleted")).toBool());
    QCOMPARE(fixture.reminderService->reminders().size(), 1);
    controller.stop();
}

void V02Test::videoCallStateAndApiTransition()
{
    VideoCallService service;
    QSignalSpy changedSpy(&service, &VideoCallService::snapshotChanged);

    const VideoCallResult started = service.startOutgoingCall();
    QVERIFY(started.success);
    QCOMPARE(started.snapshot.state, VideoCallState::OutgoingRinging);
    QVERIFY(!started.snapshot.callId.isEmpty());
    QCOMPARE(changedSpy.count(), 1);

    VideoCallActionRequest accept;
    accept.callId = started.snapshot.callId;
    accept.action = VideoCallAction::Accept;
    accept.expectedRevision = started.snapshot.revision;
    const VideoCallResult connected = service.applyRemoteAction(accept);
    QVERIFY(connected.success);
    QCOMPARE(connected.snapshot.state, VideoCallState::Connected);

    const VideoCallResult stale = service.applyRemoteAction(accept);
    QVERIFY(!stale.success);
    QCOMPARE(stale.code, VideoCallErrorCode::RevisionConflict);

    const VideoCallResult ended = service.hangUpFromDevice();
    QVERIFY(ended.success);
    QCOMPARE(ended.snapshot.state, VideoCallState::Ended);
    QCOMPARE(changedSpy.count(), 4);
}

void V02Test::mediaFrameProtocolAndIncomingCallLifecycle()
{
    const QByteArray payload = QByteArrayLiteral("pcm-test");
    const QByteArray encoded = MediaFrameProtocol::encode(
        MediaStreamType::FamilyAudio, 42, 123'456, payload, 7);
    QCOMPARE(encoded.size(), MediaFrameProtocol::HeaderSize + payload.size());
    MediaFrame decoded;
    QString error;
    QVERIFY2(MediaFrameProtocol::decode(encoded, &decoded, &error), qPrintable(error));
    QCOMPARE(decoded.version, MediaFrameProtocol::Version);
    QCOMPARE(decoded.streamType, MediaStreamType::FamilyAudio);
    QCOMPARE(decoded.sequence, 42U);
    QCOMPARE(decoded.timestampUsec, 123'456ULL);
    QCOMPARE(decoded.flags, 7U);
    QCOMPARE(decoded.payload, payload);
    QByteArray malformed = encoded;
    malformed[0] = 'X';
    QVERIFY(!MediaFrameProtocol::decode(malformed, &decoded, &error));

    FakeVideoCallMediaPort media;
    FakeCallPromptPlayer prompt;
    VideoCallService service(&media, &prompt);
    QSignalSpy activitySpy(&service, &VideoCallService::callActivityChanged);

    const VideoCallResult incoming = service.startIncomingCall(VideoCallMode::Voice);
    QVERIFY(incoming.success);
    QCOMPARE(incoming.snapshot.state, VideoCallState::NotifyingDevice);
    QCOMPARE(incoming.snapshot.direction, VideoCallDirection::FamilyToDevice);
    QCOMPARE(incoming.snapshot.mode, VideoCallMode::Voice);
    QVERIFY(media.prepared);
    QVERIFY(!media.audio);
    QVERIFY(prompt.playing);

    prompt.complete();
    QCOMPARE(service.snapshot().state, VideoCallState::ConnectingMedia);
    QVERIFY(media.audio);
    media.triggerReady();
    QCOMPARE(service.snapshot().state, VideoCallState::Connected);
    QVERIFY(service.snapshot().mediaReady);
    QVERIFY(service.snapshot().connectedAt.isValid());

    const VideoCallResult ended = service.hangUpFromDevice();
    QVERIFY(ended.success);
    QCOMPARE(ended.snapshot.state, VideoCallState::Ended);
    QVERIFY(!ended.snapshot.mediaReady);
    QCOMPARE(media.stopCount, 1);
    QCOMPARE(activitySpy.count(), 2);
}

void V02Test::pagesExposeSemanticSignalsAndModels()
{
    HomePage homePage;
    QSignalSpy videoCallSpy(&homePage, &HomePage::videoCallRequested);
    QTest::mouseClick(homePage.findChild<QPushButton*>(QStringLiteral("videoCallButton")),
                      Qt::LeftButton);
    QCOMPARE(videoCallSpy.count(), 1);
    QVERIFY(!homePage.findChild<QPushButton*>(QStringLiteral("reminderButton")));

    ReminderPage reminderPage;
    QSignalSpy addSpy(&reminderPage, &ReminderPage::addReminderRequested);
    QTest::mouseClick(reminderPage.findChild<QPushButton*>(QStringLiteral("addReminderButton")),
                      Qt::LeftButton);
    QCOMPARE(addSpy.count(), 1);

    Reminder reminder;
    reminder.id = 9;
    reminder.title = QStringLiteral("喝水");
    reminder.type = ReminderType::Water;
    reminder.timeOfDay = QTime(12, 0);
    reminderPage.setReminders({reminder});
    QCoreApplication::processEvents();
    QSignalSpy editSpy(&reminderPage, &ReminderPage::editReminderRequested);
    QSignalSpy completeSpy(&reminderPage, &ReminderPage::completeReminderRequested);
    QTest::mouseClick(reminderPage.findChild<QPushButton*>(QStringLiteral("reminderItem_9")),
                      Qt::LeftButton);
    QTest::mouseClick(reminderPage.findChild<QPushButton*>(QStringLiteral("completeReminder_9")),
                      Qt::LeftButton);
    QCOMPARE(editSpy.count(), 1);
    QCOMPARE(completeSpy.count(), 1);

    ReminderEditPage editPage;
    ReminderDraft draft = medicineDraft();
    editPage.setDraft(draft);
    QSignalSpy saveSpy(&editPage, &ReminderEditPage::saveRequested);
    QTest::mouseClick(editPage.findChild<QPushButton*>(QStringLiteral("saveReminderButton")),
                      Qt::LeftButton);
    QCOMPARE(saveSpy.count(), 1);

    CarePage carePage;
    QSignalSpy careReminderSpy(&carePage, &CarePage::reminderRequested);
    CareSummary summary;
    summary.waterCompleted = 3;
    summary.lastUpdated = QDateTime::currentDateTime();
    carePage.setSummary(summary);
    QVERIFY(carePage.findChild<QPushButton*>(QStringLiteral("recordWaterButton")));
    QTest::mouseClick(carePage.findChild<QPushButton*>(QStringLiteral("careReminderButton")),
                      Qt::LeftButton);
    QCOMPARE(careReminderSpy.count(), 1);

    SettingsPage settingsPage;
    QSignalSpy networkSetupSpy(&settingsPage, &SettingsPage::networkSetupRequested);
    UserSettings settings;
    settings.volume = 41;
    settings.brightness = 63;
    settingsPage.setSettings(settings);
    QCOMPARE(settingsPage.findChild<QSlider*>(QStringLiteral("volumeSlider"))->value(), 41);
    QCOMPARE(settingsPage.findChild<QSlider*>(QStringLiteral("brightnessSlider"))->value(), 63);

    DeviceSummary binaryBacklight;
    binaryBacklight.brightnessControlAvailable = true;
    binaryBacklight.brightnessLevels = 2;
    binaryBacklight.brightnessSummary = QStringLiteral("GPIO 背光 · 仅开/关");
    settingsPage.setDeviceSummary(binaryBacklight);
    auto* brightnessSlider = settingsPage.findChild<QSlider*>(
        QStringLiteral("brightnessSlider"));
    QCOMPARE(brightnessSlider->minimum(), 0);
    QCOMPARE(brightnessSlider->maximum(), 1);
    QCOMPARE(brightnessSlider->value(), 1);
    QTest::mouseClick(settingsPage.findChild<QPushButton*>(
                          QStringLiteral("networkSetupButton")), Qt::LeftButton);
    QCOMPARE(networkSetupSpy.count(), 1);
}

void V02Test::applicationControllerOwnsNavigation()
{
    ServiceFixture fixture;
    QString error;
    QVERIFY2(fixture.open(&error), qPrintable(error));
    MainWindow window;
    SystemService systemService;
    VideoCallService videoCallService;
    AppController controller(&window, fixture.reminderService.get(), fixture.careService.get(),
                             fixture.settingsService.get(), &systemService, 15'000,
                             nullptr, &videoCallService);
    controller.initialize();
    QCOMPARE(window.currentPage(), MainWindow::PageId::Companion);

    QTest::mouseClick(window.findChild<QPushButton*>(QStringLiteral("companionRevealButton")),
                      Qt::LeftButton);
    QCOMPARE(window.currentPage(), MainWindow::PageId::Home);
    QTest::mouseClick(window.findChild<QPushButton*>(QStringLiteral("videoCallButton")),
                      Qt::LeftButton);
    QCOMPARE(window.currentPage(), MainWindow::PageId::VideoCall);
    QCOMPARE(videoCallService.snapshot().state, VideoCallState::OutgoingRinging);
    QTest::mouseClick(window.findChild<QPushButton*>(QStringLiteral("videoCallHangUpButton")),
                      Qt::LeftButton);
    QCOMPARE(videoCallService.snapshot().state, VideoCallState::Ended);
    QTest::mouseClick(window.findChild<QPushButton*>(QStringLiteral("videoCallBackButton")),
                      Qt::LeftButton);
    QCOMPARE(window.currentPage(), MainWindow::PageId::Home);
    QTest::mouseClick(window.findChild<QPushButton*>(QStringLiteral("careButton")),
                      Qt::LeftButton);
    QCOMPARE(window.currentPage(), MainWindow::PageId::Care);
    QTest::mouseClick(window.findChild<QPushButton*>(QStringLiteral("careReminderButton")),
                      Qt::LeftButton);
    QCOMPARE(window.currentPage(), MainWindow::PageId::Reminder);
    QTest::mouseClick(window.findChild<QPushButton*>(QStringLiteral("addReminderButton")),
                      Qt::LeftButton);
    QCOMPARE(window.currentPage(), MainWindow::PageId::ReminderEdit);
}

void V02Test::statusBarRemains64AndShowsSystemInput()
{
    StatusBarWidget status(true);
    status.setFixedWidth(1024);
    SystemStatus input;
    input.currentDateTime = QDateTime(QDate(2026, 8, 14), QTime(9, 30));
    input.networkKnown = true;
    input.networkAvailable = true;
    input.networkSummary = QStringLiteral("Wi-Fi · 已联网");
    input.batteryPercent = 87;
    input.weatherSummary = QStringLiteral("晴 26°");
    status.setStatus(input);
    status.show();
    QTest::qWait(10);
    QCOMPARE(status.height(), LongPetUi::Metrics::StatusBarHeight);
    auto* button = status.findChild<QPushButton*>(QStringLiteral("settingsButton"));
    QVERIFY(button);
    QCOMPARE(button->size(), QSize(64, 64));
    const QRect geometry(button->mapTo(&status, QPoint()), button->size());
    QVERIFY(status.rect().contains(geometry));
    bool foundSummary = false;
    for (QLabel* label : status.findChildren<QLabel*>())
        foundSummary = foundSummary
            || (label->text().contains(QStringLiteral("Wi-Fi · 已联网"))
                && label->text().contains(QStringLiteral("电量 87%")));
    QVERIFY(foundSummary);

    MainWindow window;
    window.setSystemStatus(input);
    QCOMPARE(window.findChildren<StatusBarWidget*>().size(), 6);
    for (StatusBarWidget* pageStatus : window.findChildren<StatusBarWidget*>()) {
        bool pageHasSummary = false;
        for (QLabel* label : pageStatus->findChildren<QLabel*>())
            pageHasSummary = pageHasSummary
                || (label->text().contains(QStringLiteral("Wi-Fi · 已联网"))
                    && label->text().contains(QStringLiteral("电量 87%")));
        QVERIFY(pageHasSummary);
    }
}

void V02Test::controlTimeoutWorksAcrossBusinessPages()
{
    ServiceFixture fixture;
    QString error;
    QVERIFY2(fixture.open(&error), qPrintable(error));
    MainWindow window;
    SystemService systemService;
    AppController controller(&window, fixture.reminderService.get(), fixture.careService.get(),
                             fixture.settingsService.get(), &systemService, 80);
    controller.initialize();
    QTest::mouseClick(window.findChild<QPushButton*>(QStringLiteral("companionRevealButton")),
                      Qt::LeftButton);
    QTest::mouseClick(window.findChild<QPushButton*>(QStringLiteral("settingsButton")),
                      Qt::LeftButton);
    QCOMPARE(window.currentPage(), MainWindow::PageId::Settings);
    QTRY_COMPARE_WITH_TIMEOUT(window.currentPage(), MainWindow::PageId::Companion, 300);
}

void V02Test::renderV02Pages()
{
    const QString capturePath = qEnvironmentVariable("LONGPET_TEST_CAPTURE_DIR");
    if (capturePath.isEmpty())
        QSKIP("LONGPET_TEST_CAPTURE_DIR is not set");
    QVERIFY(QDir().mkpath(capturePath));

    MainWindow window;
    window.setFixedSize(1024, 600);
    QList<Reminder> reminders;
    Reminder medicine;
    medicine.id = 1;
    medicine.type = ReminderType::Medicine;
    medicine.title = QStringLiteral("早餐后吃药");
    medicine.timeOfDay = QTime(8, 0);
    medicine.status = ReminderOccurrenceStatus::Completed;
    reminders.append(medicine);
    Reminder water = medicine;
    water.id = 2;
    water.type = ReminderType::Water;
    water.title = QStringLiteral("喝一杯水");
    water.timeOfDay = QTime(12, 0);
    water.status = ReminderOccurrenceStatus::Pending;
    reminders.append(water);
    window.setReminders(reminders);
    CareSummary care;
    care.waterCompleted = 3;
    care.medicineCompleted = 1;
    care.medicineTotal = 1;
    care.lastUpdated = QDateTime::currentDateTime();
    window.setCareSummary(care);
    window.setSettings({});
    DeviceSummary device;
    device.softwareVersion = QStringLiteral("0.2.0");
    device.networkSummary = QStringLiteral("Wi-Fi · 已联网");
    device.familySummary = QStringLiteral("尚未配对");
    device.audioControlAvailable = true;
    device.audioSummary = QStringLiteral("ES8388 · ALSA 音量");
    device.brightnessControlAvailable = true;
    device.brightnessLevels = 2;
    device.brightnessSummary = QStringLiteral("GPIO 背光 · 仅开/关");
    window.setDeviceSummary(device);
    SystemStatus systemStatus;
    systemStatus.currentDateTime = QDateTime::currentDateTime();
    systemStatus.networkKnown = true;
    systemStatus.networkAvailable = true;
    systemStatus.networkSummary = QStringLiteral("Wi-Fi · 已联网");
    window.setSystemStatus(systemStatus);
    WifiNetwork wifi;
    wifi.ssid = QStringLiteral("LongPet-Test");
    wifi.signalStrength = 82;
    wifi.security = QStringLiteral("WPA2");
    wifi.requiresPassword = true;
    window.setWifiNetworks({wifi});
    VideoCallSnapshot call;
    call.callId = QStringLiteral("render-call");
    call.state = VideoCallState::OutgoingRinging;
    call.revision = 1;
    window.setVideoCallSnapshot(call);
    window.show();

    const QList<QPair<MainWindow::PageId, QString>> pages {
        {MainWindow::PageId::Companion, QStringLiteral("companion")},
        {MainWindow::PageId::Home, QStringLiteral("home")},
        {MainWindow::PageId::Care, QStringLiteral("care")},
        {MainWindow::PageId::Reminder, QStringLiteral("reminder")},
        {MainWindow::PageId::ReminderEdit, QStringLiteral("reminder-edit")},
        {MainWindow::PageId::VideoCall, QStringLiteral("video-call")},
        {MainWindow::PageId::Settings, QStringLiteral("settings")},
        {MainWindow::PageId::NetworkSetup, QStringLiteral("network-setup")}
    };
    for (const auto& page : pages) {
        window.showPage(page.first);
        QTest::qWait(30);
        const QPixmap rendered = window.grab();
        QCOMPARE(rendered.deviceIndependentSize(), QSizeF(1024, 600));
        QVERIFY(rendered.save(QDir(capturePath).filePath(page.second + QStringLiteral(".png"))));
    }
}

QTEST_MAIN(V02Test)

#include "V02Test.moc"
