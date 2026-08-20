#include "app\AppController.h"
#include "app\Application.h"
#include "data\CareEventRepository.h"
#include "data\DatabaseManager.h"
#include "data\ReminderRepository.h"
#include "data\SettingsRepository.h"
#include "mainwindow.h"
#include "pages\CarePage.h"
#include "pages\EmergencyPage.h"
#include "pages\HomePage.h"
#include "pages\ReminderEditPage.h"
#include "pages\ReminderAlertPage.h"
#include "pages\ReminderPage.h"
#include "pages\SettingsPage.h"
#include "platform\AudioVolumeAdapter.h"
#include "platform\BacklightAdapter.h"
#include "platform\KeywordSpottingAdapter.h"
#include "platform\NetworkStatusAdapter.h"
#include "platform\PowerStatusAdapter.h"
#include "services\CareService.h"
#include "services\KeywordSpottingService.h"
#include "services\ReminderService.h"
#include "services\SettingsService.h"
#include "services\SystemService.h"
#include "widgets\PetFaceWidget.h"
#include "widgets\VisualComponents.h"
#include "widgets\VisualTokens.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QSvgRenderer>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUuid>

#include <utility>

class V02Test final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void resourcesAreEmbedded();
    void databaseCreatesVersionedSchema();
    void databaseMigratesV1WithoutLosingReminders();
    void applicationCompositionRootInitializes();
    void networkStatusAdapterMapsAndDegrades();
    void hardwareAdaptersMapAndDegrade();
    void keywordSpottingProtocolMapsAndDegrades();
    void keywordSemanticsAreContextualAndDebounced();
    void reminderCrudPersistsAndRejectsStaleRevision();
    void acknowledgementStopsRepeatsAndIsNotCompletion();
    void unconfirmedReminderRepeatsUpToMaximum();
    void controllerQueuesAlertsAndRestoresPreviousPage();
    void reminderAlertPageRendersAndFallsBackIcon();
    void careAndSettingsArePersistentServices();
    void pagesExposeSemanticSignalsAndModels();
    void applicationControllerOwnsNavigation();
    void statusBarRemains64AndShowsSystemInput();
    void controlTimeoutWorksAcrossBusinessPages();
    void renderV02Pages();
};

namespace {
struct ServiceFixture {
    QTemporaryDir directory;
    DatabaseManager database;
    std::unique_ptr<ReminderRepository> reminders;
    std::unique_ptr<CareEventRepository> careEvents;
    std::unique_ptr<SettingsRepository> settingsRepository;
    std::unique_ptr<ReminderService> reminderService;
    std::unique_ptr<CareService> careService;
    std::unique_ptr<SettingsService> settingsService;

    bool open(QString* error = nullptr, ReminderService::Clock clock = {})
    {
        if (!directory.isValid())
            return false;
        if (!database.open(QDir(directory.path()).filePath(QStringLiteral("test.db")), error))
            return false;
        reminders = std::make_unique<ReminderRepository>(database.database());
        careEvents = std::make_unique<CareEventRepository>(database.database());
        settingsRepository = std::make_unique<SettingsRepository>(database.database());
        reminderService = clock
            ? std::make_unique<ReminderService>(reminders.get(), std::move(clock))
            : std::make_unique<ReminderService>(reminders.get());
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

bool createLegacyV1Database(const QString& path, QString* error)
{
    const QString connectionName = QStringLiteral("legacy-v1-%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    bool success = false;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(path);
        if (!database.open()) {
            if (error)
                *error = database.lastError().text();
        } else {
            QSqlQuery query(database);
            const QStringList statements {
                QStringLiteral("CREATE TABLE schema_meta (version INTEGER NOT NULL)"),
                QStringLiteral("INSERT INTO schema_meta(version) VALUES(1)"),
                QStringLiteral(
                    "CREATE TABLE reminders (id INTEGER PRIMARY KEY AUTOINCREMENT,"
                    "type TEXT NOT NULL,title TEXT NOT NULL,time_of_day TEXT NOT NULL,"
                    "scheduled_date TEXT,repeat_rule TEXT NOT NULL,enabled INTEGER NOT NULL,"
                    "revision INTEGER NOT NULL DEFAULT 1,created_at TEXT NOT NULL,updated_at TEXT NOT NULL)"),
                QStringLiteral(
                    "CREATE TABLE reminder_events (id INTEGER PRIMARY KEY AUTOINCREMENT,"
                    "reminder_id INTEGER NOT NULL,scheduled_at TEXT NOT NULL,status TEXT NOT NULL,"
                    "completed_at TEXT,FOREIGN KEY(reminder_id) REFERENCES reminders(id) ON DELETE CASCADE)"),
                QStringLiteral(
                    "INSERT INTO reminders(type,title,time_of_day,scheduled_date,repeat_rule,enabled,revision,created_at,updated_at) "
                    "VALUES('medicine','旧版晚饭后吃药','20:00','2026-08-20','daily',1,3,"
                    "'2026-08-01T00:00:00Z','2026-08-02T00:00:00Z')"),
                QStringLiteral(
                    "INSERT INTO reminder_events(reminder_id,scheduled_at,status,completed_at) "
                    "VALUES(1,'2026-08-20T12:00:00Z','completed','2026-08-20T12:05:00Z')")
            };
            success = true;
            for (const QString& sql : statements) {
                if (!query.exec(sql)) {
                    success = false;
                    if (error)
                        *error = query.lastError().text();
                    break;
                }
            }
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return success;
}
}

void V02Test::initTestCase()
{
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.0"));
    qRegisterMetaType<KeywordDetection>();
    qRegisterMetaType<KeywordSemantic>();
    qRegisterMetaType<KeywordSpottingStatus>();
    QFile styleFile(QStringLiteral(":/styles/app.qss"));
    QVERIFY(styleFile.open(QIODevice::ReadOnly | QIODevice::Text));
    qApp->setStyleSheet(QString::fromUtf8(styleFile.readAll()));
}

void V02Test::resourcesAreEmbedded()
{
    const QStringList resources {
        QStringLiteral(":/styles/app.qss"), QStringLiteral(":/icons/back.svg"),
        QStringLiteral(":/icons/microphone-dark.svg"), QStringLiteral(":/icons/care.svg"),
        QStringLiteral(":/icons/microphone.svg"),
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
}

void V02Test::databaseCreatesVersionedSchema()
{
    ServiceFixture fixture;
    QString error;
    QVERIFY2(fixture.open(&error), qPrintable(error));
    QCOMPARE(fixture.database.schemaVersion(), 2);

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

void V02Test::databaseMigratesV1WithoutLosingReminders()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = QDir(directory.path()).filePath(QStringLiteral("legacy.db"));
    QString error;
    QVERIFY2(createLegacyV1Database(path, &error), qPrintable(error));

    DatabaseManager database;
    QVERIFY2(database.open(path, &error), qPrintable(error));
    QCOMPARE(database.schemaVersion(), 2);
    ReminderRepository repository(database.database());
    bool found = false;
    const Reminder reminder = repository.find(1, &found, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(found);
    QCOMPARE(reminder.title, QStringLiteral("旧版晚饭后吃药"));
    QCOMPARE(reminder.revision, 3);
    QVERIFY(!reminder.uuid.isEmpty());
    QCOMPARE(reminder.iconKey, QStringLiteral("medicine"));
    QCOMPARE(reminder.voiceType, ReminderVoiceType::None);
    QCOMPARE(reminder.voiceText, reminder.title);
    QCOMPARE(reminder.repeatIntervalMinutes,
             ReminderDefaults::RepeatIntervalMinutes);
    QCOMPARE(reminder.maxPresentationCount,
             ReminderDefaults::MaxPresentationCount);
    QCOMPARE(repository.statusForDate(1, QDate(2026, 8, 20), &error),
             ReminderOccurrenceStatus::Completed);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QSqlQuery columns(database.database());
    QVERIFY(columns.exec(QStringLiteral("PRAGMA table_info(reminder_events)")));
    QStringList names;
    while (columns.next())
        names.append(columns.value(QStringLiteral("name")).toString());
    for (const QString& required : {QStringLiteral("presentation_count"),
                                    QStringLiteral("last_presented_at"),
                                    QStringLiteral("acknowledged_at"),
                                    QStringLiteral("ack_source")}) {
        QVERIFY2(names.contains(required), qPrintable(required));
    }
}

void V02Test::applicationCompositionRootInitializes()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = QDir(directory.path()).filePath(QStringLiteral("app.db"));
    QVERIFY(qputenv("LONGPET_DATABASE_PATH", databasePath.toUtf8()));
    {
        Application application;
        QString error;
        QVERIFY2(application.initialize(&error), qPrintable(error));
        QVERIFY(application.window());
        QCOMPARE(application.databasePath(), databasePath);
    }
    qunsetenv("LONGPET_DATABASE_PATH");
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

void V02Test::keywordSpottingProtocolMapsAndDegrades()
{
    KeywordSpottingAdapter::Options disabledOptions;
    disabledOptions.enabled = false;
    KeywordSpottingAdapter disabled(disabledOptions);
    QSignalSpy statusSpy(&disabled, &KeywordSpottingAdapter::statusChanged);
    QVERIFY(!disabled.start());
    QCOMPARE(statusSpy.count(), 1);
    QCOMPARE(disabled.status().state, KeywordSpottingRuntimeState::Disabled);
    QVERIFY(!disabled.status().available);
    QVERIFY(!disabled.status().listening);

    const QByteArray detectionJson = QByteArrayLiteral(
        "{\"event\":\"keyword_detected\",\"keyword\":\"知道了\","
        "\"signal\":\"ACKNOWLEDGE\",\"code\":4,\"source\":\"microphone\","
        "\"timestamp\":\"2026-08-20T10:00:00.000+08:00\"}");
    KeywordDetection detection;
    QVERIFY(KeywordSpottingAdapter::parseKeywordEvent(detectionJson, &detection));
    QCOMPARE(detection.keyword, QStringLiteral("知道了"));
    QCOMPARE(detection.signal, QStringLiteral("ACKNOWLEDGE"));
    QCOMPARE(detection.code, 4);
    QCOMPARE(detection.source, QStringLiteral("microphone"));
    QVERIFY(detection.timestamp.isValid());
    QVERIFY(!KeywordSpottingAdapter::parseKeywordEvent(
        QByteArrayLiteral("not-json"), &detection));

    KeywordSpottingStatus runtimeStatus;
    QVERIFY(KeywordSpottingAdapter::parseRuntimeStatusEvent(
        QByteArrayLiteral("{\"event\":\"runtime_status\",\"state\":\"listening\","
                          "\"detail\":\"离线关键词 · 正在监听\"}"),
        &runtimeStatus));
    QCOMPARE(runtimeStatus.state, KeywordSpottingRuntimeState::Listening);
    QVERIFY(runtimeStatus.available);
    QVERIFY(runtimeStatus.listening);
}

void V02Test::keywordSemanticsAreContextualAndDebounced()
{
    QDateTime current(QDate(2026, 8, 20), QTime(10, 0));
    KeywordSpottingService keywordService(nullptr, [&current] { return current; });
    QSignalSpy semanticSpy(&keywordService,
                           &KeywordSpottingService::semanticDetected);
    KeywordDetection detection;
    detection.keyword = QStringLiteral("知道了");
    detection.signal = QStringLiteral("ACKNOWLEDGE");
    detection.timestamp = current;
    keywordService.handleDetection(detection);
    QCOMPARE(semanticSpy.count(), 1);
    QCOMPARE(qvariant_cast<KeywordSemantic>(semanticSpy.first().at(0)),
             KeywordSemantic::Acknowledge);
    keywordService.handleDetection(detection);
    QCOMPARE(semanticSpy.count(), 1);
    current = current.addMSecs(KeywordSpottingService::defaultCooldownMs());
    keywordService.handleDetection(detection);
    QCOMPARE(semanticSpy.count(), 2);

    ServiceFixture fixture;
    QString error;
    QVERIFY2(fixture.open(&error, [&current] { return current; }), qPrintable(error));
    ReminderDraft first = medicineDraft();
    first.timeOfDay = current.time();
    QVERIFY(fixture.reminderService->save(first).success);
    MainWindow window;
    SystemService systemService;
    KeywordSpottingService controllerKeywordService(
        nullptr, [&current] { return current; });
    AppController controller(&window, fixture.reminderService.get(),
                             fixture.careService.get(), fixture.settingsService.get(),
                             &systemService, 15'000, &controllerKeywordService);
    controller.initialize();
    QVERIFY(controller.hasActiveReminderAlert());
    const ReminderEventId firstEventId = controller.currentReminderEventId();
    KeywordDetection controllerDetection;
    controllerDetection.keyword = QStringLiteral("知道了");
    controllerDetection.signal = QStringLiteral("ACKNOWLEDGE");
    controllerDetection.timestamp = current;
    controllerKeywordService.handleDetection(controllerDetection);
    QVERIFY(!controller.hasActiveReminderAlert());
    bool found = false;
    ReminderOccurrence firstOccurrence = fixture.reminders->occurrence(
        firstEventId, &found, &error);
    QVERIFY(found);
    QCOMPARE(firstOccurrence.status, ReminderOccurrenceStatus::Acknowledged);
    QCOMPARE(firstOccurrence.ackSource, ReminderAckSource::Voice);
    QVERIFY(!firstOccurrence.completedAt.isValid());
    QVERIFY(!controller.handleKeywordSemantic(KeywordSemantic::Acknowledge,
                                              QStringLiteral("好的")));

    controllerDetection.keyword = QStringLiteral("你好");
    controllerDetection.signal = QStringLiteral("GREETING");
    controllerKeywordService.handleDetection(controllerDetection);
    QCOMPARE(window.currentPage(), MainWindow::PageId::Home);
    controllerDetection.keyword = QStringLiteral("救命");
    controllerDetection.signal = QStringLiteral("EMERGENCY");
    controllerKeywordService.handleDetection(controllerDetection);
    QCOMPARE(window.currentPage(), MainWindow::PageId::Emergency);
    window.findChild<QPushButton*>(
        QStringLiteral("emergencyDismissButton"))->click();
    QCOMPARE(window.currentPage(), MainWindow::PageId::Home);

    ReminderDraft second = first;
    second.title = QStringLiteral("第二次吃药");
    const ServiceResult secondSaved = fixture.reminderService->save(second);
    QVERIFY2(secondSaved.success, qPrintable(secondSaved.error));
    fixture.reminderService->checkNow();
    QVERIFY(controller.hasActiveReminderAlert());
    const ReminderEventId secondEventId = controller.currentReminderEventId();
    QVERIFY(controller.handleKeywordSemantic(KeywordSemantic::Emergency,
                                             QStringLiteral("救命")));
    QCOMPARE(window.currentPage(), MainWindow::PageId::Emergency);
    window.findChild<QPushButton*>(
        QStringLiteral("emergencyDismissButton"))->click();
    QCOMPARE(window.currentPage(), MainWindow::PageId::ReminderAlert);
    controllerDetection.keyword = QStringLiteral("吃过了");
    controllerDetection.signal = QStringLiteral("COMPLETE");
    controllerKeywordService.handleDetection(controllerDetection);
    QVERIFY(!controller.hasActiveReminderAlert());
    const ReminderOccurrence secondOccurrence = fixture.reminders->occurrence(
        secondEventId, &found, &error);
    QVERIFY(found);
    QCOMPARE(secondOccurrence.status, ReminderOccurrenceStatus::Completed);
    QCOMPARE(secondOccurrence.ackSource, ReminderAckSource::Voice);
    QVERIFY(secondOccurrence.completedAt.isValid());

    QSignalSpy stopSpy(&controller, &AppController::stopVoicePlaybackRequested);
    controllerDetection.keyword = QStringLiteral("停止");
    controllerDetection.signal = QStringLiteral("STOP");
    controllerKeywordService.handleDetection(controllerDetection);
    QCOMPARE(stopSpy.count(), 1);
    fixture.reminderService->stop();
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
    QVERIFY(!stored.uuid.isEmpty());
    QCOMPARE(stored.iconKey, QStringLiteral("medicine"));
    QCOMPARE(stored.voiceText, stored.title);

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

void V02Test::acknowledgementStopsRepeatsAndIsNotCompletion()
{
    QDateTime current(QDate(2026, 8, 20), QTime(8, 0));
    ServiceFixture fixture;
    QString error;
    QVERIFY2(fixture.open(&error, [&current] { return current; }), qPrintable(error));
    ReminderDraft draft = medicineDraft();
    draft.timeOfDay = current.time();
    draft.repeatIntervalMinutes = 5;
    draft.maxPresentationCount = 3;
    const ServiceResult saved = fixture.reminderService->save(draft);
    QVERIFY2(saved.success, qPrintable(saved.error));

    QSignalSpy presentationSpy(fixture.reminderService.get(),
        &ReminderService::reminderPresentationRequested);
    fixture.reminderService->start();
    QCOMPARE(presentationSpy.count(), 1);
    const ReminderPresentation presentation = qvariant_cast<ReminderPresentation>(
        presentationSpy.first().first());
    QCOMPARE(presentation.occurrence.status,
             ReminderOccurrenceStatus::Presented);
    QCOMPARE(presentation.occurrence.presentationCount, 1);

    const ServiceResult acknowledged = fixture.reminderService->acknowledgeOccurrence(
        presentation.occurrence.id, ReminderAckSource::Touch);
    QVERIFY2(acknowledged.success, qPrintable(acknowledged.error));
    bool found = false;
    const ReminderOccurrence occurrence = fixture.reminders->occurrence(
        presentation.occurrence.id, &found, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(found);
    QCOMPARE(occurrence.status, ReminderOccurrenceStatus::Acknowledged);
    QCOMPARE(occurrence.ackSource, ReminderAckSource::Touch);
    QVERIFY(occurrence.acknowledgedAt.isValid());
    QVERIFY(!occurrence.completedAt.isValid());

    const CareSummary summary = fixture.careService->todaySummary(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(summary.medicineTotal, 1);
    QCOMPARE(summary.medicineCompleted, 0);

    current = current.addSecs(10 * 60);
    fixture.reminderService->checkNow();
    QCOMPARE(presentationSpy.count(), 1);
    QCOMPARE(fixture.reminders->occurrence(
                 presentation.occurrence.id, &found).presentationCount, 1);
    fixture.reminderService->stop();
}

void V02Test::unconfirmedReminderRepeatsUpToMaximum()
{
    QDateTime current(QDate(2026, 8, 20), QTime(10, 0));
    ServiceFixture fixture;
    QString error;
    QVERIFY2(fixture.open(&error, [&current] { return current; }), qPrintable(error));
    ReminderDraft draft = medicineDraft();
    draft.timeOfDay = current.time();
    draft.repeatIntervalMinutes = 1;
    draft.maxPresentationCount = 3;
    QVERIFY(fixture.reminderService->save(draft).success);

    QSignalSpy presentationSpy(fixture.reminderService.get(),
        &ReminderService::reminderPresentationRequested);
    fixture.reminderService->start();
    QCOMPARE(presentationSpy.count(), 1);
    const ReminderEventId eventId = qvariant_cast<ReminderPresentation>(
        presentationSpy.first().first()).occurrence.id;

    current = current.addSecs(60);
    fixture.reminderService->checkNow();
    QCOMPARE(presentationSpy.count(), 2);
    current = current.addSecs(60);
    fixture.reminderService->checkNow();
    QCOMPARE(presentationSpy.count(), 3);
    bool found = false;
    QCOMPARE(fixture.reminders->occurrence(eventId, &found, &error).presentationCount, 3);
    QVERIFY(found);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    current = current.addSecs(60);
    fixture.reminderService->checkNow();
    QCOMPARE(presentationSpy.count(), 3);
    const ReminderOccurrence missed = fixture.reminders->occurrence(eventId, &found, &error);
    QCOMPARE(missed.status, ReminderOccurrenceStatus::Missed);
    QCOMPARE(missed.presentationCount, 3);
    fixture.reminderService->stop();
}

void V02Test::controllerQueuesAlertsAndRestoresPreviousPage()
{
    QDateTime current(QDate(2026, 8, 20), QTime(8, 0));
    ServiceFixture fixture;
    QString error;
    QVERIFY2(fixture.open(&error, [&current] { return current; }), qPrintable(error));

    ReminderDraft medicine = medicineDraft();
    medicine.timeOfDay = QTime(9, 0);
    QVERIFY(fixture.reminderService->save(medicine).success);
    ReminderDraft water = medicine;
    water.type = ReminderType::Water;
    water.title = QStringLiteral("喝一杯水");
    water.iconKey = QStringLiteral("water");
    QVERIFY(fixture.reminderService->save(water).success);

    MainWindow window;
    window.setFixedSize(1024, 600);
    window.show();
    QTest::qWait(10);
    SystemService systemService;
    AppController controller(&window, fixture.reminderService.get(),
                             fixture.careService.get(), fixture.settingsService.get(),
                             &systemService, 15'000);
    controller.initialize();
    window.showPage(MainWindow::PageId::Settings);
    QCOMPARE(window.currentPage(), MainWindow::PageId::Settings);

    current = QDateTime(current.date(), QTime(9, 0));
    fixture.reminderService->checkNow();
    QCOMPARE(window.currentPage(), MainWindow::PageId::ReminderAlert);
    QVERIFY(window.currentPage() != MainWindow::PageId::Reminder);
    QVERIFY(controller.hasActiveReminderAlert());
    const ReminderEventId firstEvent = controller.currentReminderEventId();
    QVERIFY(firstEvent != 0);

    auto* acknowledge = window.findChild<QPushButton*>(
        QStringLiteral("acknowledgeAlertButton"));
    QVERIFY(acknowledge);
    QTest::mouseClick(acknowledge, Qt::LeftButton);
    QCOMPARE(window.currentPage(), MainWindow::PageId::ReminderAlert);
    QVERIFY(controller.hasActiveReminderAlert());
    const ReminderEventId secondEvent = controller.currentReminderEventId();
    QVERIFY(secondEvent != 0);
    QVERIFY(secondEvent != firstEvent);

    bool found = false;
    ReminderOccurrence first = fixture.reminders->occurrence(
        firstEvent, &found, &error);
    QVERIFY(found);
    QCOMPARE(first.status, ReminderOccurrenceStatus::Acknowledged);
    QVERIFY(!first.completedAt.isValid());

    QTest::mouseClick(acknowledge, Qt::LeftButton);
    QVERIFY(!controller.hasActiveReminderAlert());
    QCOMPARE(window.currentPage(), MainWindow::PageId::Settings);
    const ReminderOccurrence second = fixture.reminders->occurrence(
        secondEvent, &found, &error);
    QVERIFY(found);
    QCOMPARE(second.status, ReminderOccurrenceStatus::Acknowledged);
    QVERIFY(!controller.handleReminderConfirmation(
        ReminderConfirmationSemantic::Acknowledge, ReminderAckSource::Voice));
    fixture.reminderService->stop();
}

void V02Test::reminderAlertPageRendersAndFallsBackIcon()
{
    QCOMPARE(reminderIconResourcePath(QStringLiteral("invalid-icon-key")),
             QStringLiteral(":/icons/reminder.svg"));
    QCOMPARE(reminderIconResourcePath(QStringLiteral("water")),
             QStringLiteral(":/icons/water.svg"));

    ReminderPresentation presentation;
    presentation.reminder.id = 1;
    presentation.reminder.title = QStringLiteral("该吃药了");
    presentation.reminder.iconKey = QStringLiteral("invalid-icon-key");
    presentation.occurrence.id = 7;
    presentation.occurrence.scheduledAt = QDateTime(
        QDate(2026, 8, 20), QTime(12, 0));
    presentation.occurrence.status = ReminderOccurrenceStatus::Presented;
    presentation.occurrence.presentationCount = 1;

    ReminderAlertPage page;
    page.setFixedSize(1024, 600);
    page.setPresentation(presentation);
    QSignalSpy acknowledgeSpy(&page, &ReminderAlertPage::acknowledgeRequested);
    page.show();
    QTest::qWait(10);
    QCOMPARE(page.size(), QSize(1024, 600));
    QCOMPARE(page.findChild<QLabel*>(QStringLiteral("reminderAlertText"))->text(),
             presentation.reminder.title);
    const QPixmap rendered = page.grab();
    QCOMPARE(rendered.deviceIndependentSize(), QSizeF(1024, 600));
    QVERIFY(!rendered.isNull());
    QTest::mouseClick(page.findChild<QPushButton*>(
        QStringLiteral("acknowledgeAlertButton")), Qt::LeftButton);
    QCOMPARE(acknowledgeSpy.count(), 1);
    QCOMPARE(acknowledgeSpy.first().first().toLongLong(), 7);
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

void V02Test::pagesExposeSemanticSignalsAndModels()
{
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
    CareSummary summary;
    summary.waterCompleted = 3;
    summary.lastUpdated = QDateTime::currentDateTime();
    carePage.setSummary(summary);
    QVERIFY(carePage.findChild<QPushButton*>(QStringLiteral("recordWaterButton")));

    SettingsPage settingsPage;
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
    DeviceSummary keywordSummary = binaryBacklight;
    keywordSummary.keywordSpottingAvailable = true;
    keywordSummary.keywordSpottingListening = true;
    keywordSummary.keywordSpottingSummary = QStringLiteral("离线关键词 · 正在监听");
    keywordSummary.lastKeyword = QStringLiteral("你好");
    settingsPage.setDeviceSummary(keywordSummary);
    const auto* keywordLabel = settingsPage.findChild<QLabel*>(
        QStringLiteral("keywordSpottingSummary"));
    QVERIFY(keywordLabel);
    QVERIFY(keywordLabel->text().contains(QStringLiteral("正在监听")));
    QVERIFY(keywordLabel->text().contains(QStringLiteral("你好")));
}

void V02Test::applicationControllerOwnsNavigation()
{
    ServiceFixture fixture;
    QString error;
    QVERIFY2(fixture.open(&error), qPrintable(error));
    MainWindow window;
    SystemService systemService;
    AppController controller(&window, fixture.reminderService.get(), fixture.careService.get(),
                             fixture.settingsService.get(), &systemService, 15'000);
    controller.initialize();
    QCOMPARE(window.currentPage(), MainWindow::PageId::Companion);

    QTest::mouseClick(window.findChild<QPushButton*>(QStringLiteral("companionRevealButton")),
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
    QCOMPARE(window.findChildren<StatusBarWidget*>().size(), 5);
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
    ReminderPresentation alert;
    alert.reminder = medicine;
    alert.reminder.title = QStringLiteral("早餐后吃药");
    alert.reminder.iconKey = QStringLiteral("medicine");
    alert.occurrence.id = 99;
    alert.occurrence.reminderId = medicine.id;
    alert.occurrence.scheduledAt = QDateTime::currentDateTime();
    alert.occurrence.status = ReminderOccurrenceStatus::Presented;
    alert.occurrence.presentationCount = 1;
    window.setReminderPresentation(alert);
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
    device.keywordSpottingAvailable = true;
    device.keywordSpottingListening = true;
    device.keywordSpottingSummary = QStringLiteral("离线关键词 · 正在监听");
    window.setDeviceSummary(device);
    SystemStatus systemStatus;
    systemStatus.currentDateTime = QDateTime::currentDateTime();
    systemStatus.networkKnown = true;
    systemStatus.networkAvailable = true;
    systemStatus.networkSummary = QStringLiteral("Wi-Fi · 已联网");
    window.setSystemStatus(systemStatus);
    window.show();

    const QList<QPair<MainWindow::PageId, QString>> pages {
        {MainWindow::PageId::Companion, QStringLiteral("companion")},
        {MainWindow::PageId::Home, QStringLiteral("home")},
        {MainWindow::PageId::Care, QStringLiteral("care")},
        {MainWindow::PageId::Reminder, QStringLiteral("reminder")},
        {MainWindow::PageId::ReminderEdit, QStringLiteral("reminder-edit")},
        {MainWindow::PageId::Settings, QStringLiteral("settings")},
        {MainWindow::PageId::ReminderAlert, QStringLiteral("reminder-alert")}
        ,{MainWindow::PageId::Emergency, QStringLiteral("emergency")}
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
