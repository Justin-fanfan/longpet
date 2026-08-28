#include "AppController.h"

#include "mainwindow.h"
#include "services/CareService.h"
#include "services/NetworkService.h"
#include "services/ReminderService.h"
#include "services/SettingsService.h"
#include "services/SystemService.h"

AppController::AppController(MainWindow* window,
                             ReminderService* reminderService,
                             CareService* careService,
                             SettingsService* settingsService,
                             SystemService* systemService,
                             int controlTimeoutMs,
                             NetworkService* networkService,
                             QObject* parent)
    : QObject(parent),
      m_window(window),
      m_reminderService(reminderService),
      m_careService(careService),
      m_settingsService(settingsService),
      m_systemService(systemService),
      m_networkService(networkService)
{
    m_controlTimeout.setObjectName(QStringLiteral("controlTimeout"));
    m_controlTimeout.setSingleShot(true);
    m_controlTimeout.setInterval(qMax(1, controlTimeoutMs));
    connect(&m_controlTimeout, &QTimer::timeout, this, [this] {
        showPage(MainWindow::PageId::Companion);
    });
    connectUi();
    connectServices();
}

void AppController::initialize()
{
    m_window->setSystemStatus(m_systemService->status());
    m_window->setDeviceSummary(m_systemService->deviceSummary());
    refreshReminders();
    refreshCare();
    refreshSettings();
    showPage(MainWindow::PageId::Companion);
    m_reminderService->start();
}

void AppController::connectUi()
{
    connect(m_window, &MainWindow::controlRequested,
            this, &AppController::showHome);
    connect(m_window, &MainWindow::userActivity, this,
            [this](MainWindow::PageId page) {
        if (page != MainWindow::PageId::Companion)
            m_controlTimeout.start();
    });
    connect(m_window, &MainWindow::homeRequested,
            this, &AppController::showHome);
    connect(m_window, &MainWindow::talkRequested, this, [this] {
        m_window->showToast(QStringLiteral("语音能力将在 V0.3 接入"));
    });
    connect(m_window, &MainWindow::careRequested,
            this, &AppController::showCare);
    connect(m_window, &MainWindow::reminderRequested,
            this, &AppController::showReminders);
    connect(m_window, &MainWindow::settingsRequested,
            this, &AppController::showSettings);
    connect(m_window, &MainWindow::addReminderRequested, this, [this] {
        ReminderDraft draft;
        draft.timeOfDay = QTime::currentTime().addSecs(3600);
        draft.scheduledDate = QDate::currentDate();
        m_window->setReminderDraft(draft);
        showPage(MainWindow::PageId::ReminderEdit);
    });
    connect(m_window, &MainWindow::editReminderRequested,
            this, &AppController::editReminder);
    connect(m_window, &MainWindow::saveReminderRequested,
            this, &AppController::saveReminder);
    connect(m_window, &MainWindow::deleteReminderRequested,
            this, &AppController::deleteReminder);
    connect(m_window, &MainWindow::cancelReminderEditRequested,
            this, &AppController::showReminders);
    connect(m_window, &MainWindow::completeReminderRequested,
            this, &AppController::completeReminder);
    connect(m_window, &MainWindow::recordWaterRequested, this, [this] {
        const ServiceResult result = m_careService->recordWater();
        if (!result.success) {
            m_window->showToast(result.error);
            return;
        }
        refreshCare();
        m_window->showToast(QStringLiteral("已记录一杯水"));
    });
    connect(m_window, &MainWindow::volumeChangeRequested, this, [this](int value) {
        QString error;
        if (!m_settingsService->setVolume(value, &error))
            showDataError(QStringLiteral("保存音量"), error);
        else
            m_window->showToast(QStringLiteral("音量设置已保存"));
    });
    connect(m_window, &MainWindow::brightnessChangeRequested, this, [this](int value) {
        QString error;
        if (!m_settingsService->setBrightness(value, &error))
            showDataError(QStringLiteral("保存亮度"), error);
        else
            m_window->showToast(QStringLiteral("背光设置已保存"));
    });
    connect(m_window, &MainWindow::networkSetupRequested,
            this, &AppController::showNetworkSetup);
    connect(m_window, &MainWindow::wifiScanRequested, this, [this] {
        if (m_networkService)
            m_networkService->scanNetworks();
    });
    connect(m_window, &MainWindow::wifiConnectRequested, this,
            [this](const WifiNetwork& network, const QString& password) {
        if (m_networkService)
            m_networkService->connectToNetwork(network, password);
    });
    connect(m_window, &MainWindow::petStyleChangeRequested, this, [this](const QString& style) {
        QString error;
        if (!m_settingsService->setPetStyle(style, &error))
            showDataError(QStringLiteral("保存宠物风格"), error);
        else
            m_window->showToast(QStringLiteral("宠物风格已保存"));
    });
    connect(m_window, &MainWindow::pairFamilyRequested, this, [this] {
        m_window->showToast(QStringLiteral("家属配对通信将在后续版本接入"));
    });
}

void AppController::connectServices()
{
    connect(m_reminderService, &ReminderService::remindersChanged,
            this, &AppController::refreshReminders);
    connect(m_reminderService, &ReminderService::reminderTriggered,
            this, [this](const Reminder& reminder) {
                refreshReminders();
                refreshCare();
                showPage(MainWindow::PageId::Reminder);
                m_window->showToast(QStringLiteral("提醒：%1").arg(reminder.title));
            });
    connect(m_reminderService, &ReminderService::errorOccurred,
            this, [this](const QString& error) {
                showDataError(QStringLiteral("提醒调度"), error);
            });
    connect(m_careService, &CareService::summaryChanged,
            this, &AppController::refreshCare);
    connect(m_settingsService, &SettingsService::settingsChanged,
            m_window, &MainWindow::setSettings);
    connect(m_systemService, &SystemService::statusChanged,
            m_window, &MainWindow::setSystemStatus);
    connect(m_systemService, &SystemService::deviceSummaryChanged,
            m_window, &MainWindow::setDeviceSummary);
    if (m_networkService) {
        connect(m_networkService, &NetworkService::scanStarted,
                m_window, &MainWindow::setWifiScanStarted);
        connect(m_networkService, &NetworkService::networksChanged,
                m_window, &MainWindow::setWifiNetworks);
        connect(m_networkService, &NetworkService::scanFailed,
                m_window, &MainWindow::setWifiScanFailed);
        connect(m_networkService, &NetworkService::connectionStarted,
                m_window, &MainWindow::setWifiConnectionStarted);
        connect(m_networkService, &NetworkService::connectionStarted,
                this, [this] { m_controlTimeout.stop(); });
        connect(m_networkService, &NetworkService::connectionSucceeded,
                m_window, &MainWindow::setWifiConnectionSucceeded);
        connect(m_networkService, &NetworkService::connectionSucceeded,
                this, [this](const QString& ssid) {
            m_window->showToast(QStringLiteral("已连接到 %1").arg(ssid));
            if (m_window->currentPage() != MainWindow::PageId::Companion)
                m_controlTimeout.start();
        });
        connect(m_networkService, &NetworkService::connectionFailed,
                m_window, &MainWindow::setWifiConnectionFailed);
        connect(m_networkService, &NetworkService::connectionFailed,
                this, [this](const QString&, const QString& error) {
            m_window->showToast(QStringLiteral("连接 Wi-Fi 失败：%1").arg(error));
            if (m_window->currentPage() != MainWindow::PageId::Companion)
                m_controlTimeout.start();
        });
    }
}

void AppController::showHome()
{
    showPage(MainWindow::PageId::Home);
}

void AppController::showPage(MainWindow::PageId page)
{
    m_window->showPage(page);
    if (page == MainWindow::PageId::Companion)
        m_controlTimeout.stop();
    else
        m_controlTimeout.start();
}

void AppController::showCare()
{
    refreshCare();
    showPage(MainWindow::PageId::Care);
}

void AppController::showReminders()
{
    refreshReminders();
    showPage(MainWindow::PageId::Reminder);
}

void AppController::showSettings()
{
    refreshSettings();
    m_window->setDeviceSummary(m_systemService->deviceSummary());
    showPage(MainWindow::PageId::Settings);
}

void AppController::showNetworkSetup()
{
    if (!m_networkService) {
        m_window->showToast(QStringLiteral("网络配置服务不可用"));
        return;
    }
    showPage(MainWindow::PageId::NetworkSetup);
    m_networkService->scanNetworks();
}

void AppController::editReminder(ReminderId id)
{
    bool found = false;
    QString error;
    const Reminder reminder = m_reminderService->reminder(id, &found, &error);
    if (!error.isEmpty() || !found) {
        showDataError(QStringLiteral("读取提醒"), error.isEmpty()
            ? QStringLiteral("提醒不存在") : error);
        return;
    }
    ReminderDraft draft;
    draft.id = reminder.id;
    draft.type = reminder.type;
    draft.title = reminder.title;
    draft.timeOfDay = reminder.timeOfDay;
    draft.scheduledDate = reminder.scheduledDate;
    draft.repeatRule = reminder.repeatRule;
    draft.enabled = reminder.enabled;
    draft.expectedRevision = reminder.revision;
    m_window->setReminderDraft(draft);
    showPage(MainWindow::PageId::ReminderEdit);
}

void AppController::saveReminder(const ReminderDraft& draft)
{
    const ServiceResult result = m_reminderService->save(draft);
    if (!result.success) {
        m_window->showToast(result.error);
        return;
    }
    refreshReminders();
    refreshCare();
    showPage(MainWindow::PageId::Reminder);
    m_window->showToast(draft.id == 0
        ? QStringLiteral("提醒已创建") : QStringLiteral("提醒已更新"));
}

void AppController::deleteReminder(ReminderId id)
{
    const ServiceResult result = m_reminderService->remove(id);
    if (!result.success) {
        m_window->showToast(result.error);
        return;
    }
    refreshReminders();
    refreshCare();
    showPage(MainWindow::PageId::Reminder);
    m_window->showToast(QStringLiteral("提醒已删除"));
}

void AppController::completeReminder(ReminderId id)
{
    const ServiceResult result = m_reminderService->markCompleted(id);
    if (!result.success) {
        m_window->showToast(result.error);
        return;
    }
    refreshReminders();
    refreshCare();
    m_window->showToast(QStringLiteral("已标记完成"));
}

void AppController::refreshReminders()
{
    QString error;
    const QList<Reminder> reminders = m_reminderService->reminders(&error);
    if (!error.isEmpty())
        showDataError(QStringLiteral("刷新提醒"), error);
    m_window->setReminders(reminders);
}

void AppController::refreshCare()
{
    QString error;
    const CareSummary summary = m_careService->todaySummary(&error);
    if (!error.isEmpty())
        showDataError(QStringLiteral("刷新关怀摘要"), error);
    m_window->setCareSummary(summary);
}

void AppController::refreshSettings()
{
    QString error;
    const UserSettings settings = m_settingsService->settings(&error);
    if (!error.isEmpty())
        showDataError(QStringLiteral("读取设置"), error);
    m_window->setSettings(settings);
}

void AppController::showDataError(const QString& context, const QString& error)
{
    m_window->showToast(QStringLiteral("%1失败：%2").arg(context, error));
}
