#include "AppController.h"

#include "mainwindow.h"
#include "services/CareService.h"
#include "services/KeywordSpottingService.h"
#include "services/ReminderService.h"
#include "services/SettingsService.h"
#include "services/SystemService.h"

AppController::AppController(MainWindow* window,
                             ReminderService* reminderService,
                             CareService* careService,
                             SettingsService* settingsService,
                             SystemService* systemService,
                             int controlTimeoutMs,
                             KeywordSpottingService* keywordSpottingService,
                             QObject* parent)
    : QObject(parent),
      m_window(window),
      m_reminderService(reminderService),
      m_careService(careService),
      m_settingsService(settingsService),
      m_systemService(systemService),
      m_keywordSpottingService(keywordSpottingService)
{
    m_controlTimeout.setObjectName(QStringLiteral("controlTimeout"));
    m_controlTimeout.setSingleShot(true);
    m_controlTimeout.setInterval(qMax(1, controlTimeoutMs));
    connect(&m_controlTimeout, &QTimer::timeout, this, [this] {
        if (!hasActiveReminderAlert())
            showPage(MainWindow::PageId::Companion);
    });

    m_alertPresentationTimeout.setObjectName(
        QStringLiteral("reminderPresentationTimeout"));
    m_alertPresentationTimeout.setSingleShot(true);
    m_alertPresentationTimeout.setInterval(
        m_reminderService->presentationDurationMs());
    connect(&m_alertPresentationTimeout, &QTimer::timeout,
            this, &AppController::expireCurrentReminderPresentation);
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

bool AppController::hasActiveReminderAlert() const
{
    return m_currentPresentation.occurrence.id != 0;
}

ReminderEventId AppController::currentReminderEventId() const
{
    return m_currentPresentation.occurrence.id;
}

bool AppController::handleReminderConfirmation(
    ReminderConfirmationSemantic semantic, ReminderAckSource source)
{
    if (!hasActiveReminderAlert())
        return false;
    const ReminderEventId eventId = m_currentPresentation.occurrence.id;
    const ServiceResult result = semantic == ReminderConfirmationSemantic::Complete
        ? m_reminderService->completeOccurrence(eventId, source)
        : m_reminderService->acknowledgeOccurrence(eventId, source);
    if (!result.success) {
        m_window->showToast(result.error);
        return false;
    }
    finishCurrentReminderPresentation();
    return true;
}

bool AppController::handleKeywordSemantic(KeywordSemantic semantic,
                                          const QString& keyword)
{
    switch (semantic) {
    case KeywordSemantic::Acknowledge:
        return handleReminderConfirmation(ReminderConfirmationSemantic::Acknowledge,
                                          ReminderAckSource::Voice);
    case KeywordSemantic::Complete:
        return handleReminderConfirmation(ReminderConfirmationSemantic::Complete,
                                          ReminderAckSource::Voice);
    case KeywordSemantic::Greeting:
        if (m_emergencyActive || hasActiveReminderAlert())
            return false;
        showHome();
        m_window->showToast(keyword.isEmpty()
            ? QStringLiteral("我在呢")
            : QStringLiteral("听到“%1”，我在呢").arg(keyword));
        return true;
    case KeywordSemantic::Emergency:
        showEmergency();
        return true;
    case KeywordSemantic::Stop:
        emit stopVoicePlaybackRequested();
        m_window->showToast(QStringLiteral("已收到停止指令"));
        return true;
    case KeywordSemantic::Unknown:
        return false;
    }
    return false;
}

void AppController::connectUi()
{
    connect(m_window, &MainWindow::controlRequested,
            this, &AppController::showHome);
    connect(m_window, &MainWindow::userActivity, this,
            [this](MainWindow::PageId page) {
        if (page != MainWindow::PageId::Companion
            && page != MainWindow::PageId::ReminderAlert) {
            m_controlTimeout.start();
        }
    });
    connect(m_window, &MainWindow::homeRequested,
            this, &AppController::showHome);
    connect(m_window, &MainWindow::talkRequested, this, [this] {
        m_window->showToast(QStringLiteral("语音能力将在后续版本接入"));
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
    connect(m_window, &MainWindow::acknowledgeReminderAlertRequested,
            this, [this](ReminderEventId eventId) {
        if (eventId == currentReminderEventId())
            handleReminderConfirmation(ReminderConfirmationSemantic::Acknowledge,
                                       ReminderAckSource::Touch);
    });
    connect(m_window, &MainWindow::completeReminderAlertRequested,
            this, [this](ReminderEventId eventId) {
        if (eventId == currentReminderEventId())
            handleReminderConfirmation(ReminderConfirmationSemantic::Complete,
                                       ReminderAckSource::Touch);
    });
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
    connect(m_window, &MainWindow::emergencyDismissRequested,
            this, &AppController::dismissEmergency);
    connect(m_window, &MainWindow::emergencyContactRequested, this, [this] {
        m_window->showToast(QStringLiteral("真实呼叫尚未接入，请立即使用电话或现场求助"));
    });
}

void AppController::connectServices()
{
    connect(m_reminderService, &ReminderService::remindersChanged,
            this, &AppController::refreshReminders);
    connect(m_reminderService, &ReminderService::reminderPresentationRequested,
            this, &AppController::enqueueReminderPresentation);
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
    if (m_keywordSpottingService) {
        connect(m_keywordSpottingService, &KeywordSpottingService::semanticDetected,
                this, &AppController::handleKeywordSemantic);
    }
}

void AppController::showHome()
{
    showPage(MainWindow::PageId::Home);
}

void AppController::showPage(MainWindow::PageId page)
{
    if (m_emergencyActive && page != MainWindow::PageId::Emergency)
        return;
    if (hasActiveReminderAlert() && page != MainWindow::PageId::ReminderAlert)
        return;
    m_window->showPage(page);
    if (page == MainWindow::PageId::Companion
        || page == MainWindow::PageId::ReminderAlert) {
        m_controlTimeout.stop();
    } else {
        m_controlTimeout.start();
    }
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
    draft.uuid = reminder.uuid;
    draft.type = reminder.type;
    draft.title = reminder.title;
    draft.iconKey = reminder.iconKey;
    draft.voiceType = reminder.voiceType;
    draft.voiceText = reminder.voiceText;
    draft.voiceAssetId = reminder.voiceAssetId;
    draft.timeOfDay = reminder.timeOfDay;
    draft.scheduledDate = reminder.scheduledDate;
    draft.repeatRule = reminder.repeatRule;
    draft.repeatIntervalMinutes = reminder.repeatIntervalMinutes;
    draft.maxPresentationCount = reminder.maxPresentationCount;
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

void AppController::enqueueReminderPresentation(
    const ReminderPresentation& presentation)
{
    const ReminderEventId eventId = presentation.occurrence.id;
    if (eventId == 0 || eventId == currentReminderEventId()
        || m_queuedEventIds.contains(eventId)) {
        return;
    }
    m_pendingPresentations.enqueue(presentation);
    m_queuedEventIds.insert(eventId);
    if (!hasActiveReminderAlert())
        showNextReminderPresentation();
}

void AppController::showNextReminderPresentation()
{
    if (m_emergencyActive || hasActiveReminderAlert()
        || m_pendingPresentations.isEmpty())
        return;
    if (!m_hasPageBeforeReminder) {
        m_pageBeforeReminder = m_window->currentPage();
        if (m_pageBeforeReminder == MainWindow::PageId::ReminderAlert)
            m_pageBeforeReminder = MainWindow::PageId::Companion;
        m_hasPageBeforeReminder = true;
    }
    m_currentPresentation = m_pendingPresentations.dequeue();
    m_queuedEventIds.remove(m_currentPresentation.occurrence.id);
    m_window->setReminderPresentation(m_currentPresentation);
    showPage(MainWindow::PageId::ReminderAlert);
    m_alertPresentationTimeout.start();
    emit reminderVoicePlaybackRequested(m_currentPresentation.reminder);
}

void AppController::finishCurrentReminderPresentation()
{
    m_alertPresentationTimeout.stop();
    m_currentPresentation = {};
    m_window->clearReminderPresentation();
    refreshReminders();
    refreshCare();
    if (!m_pendingPresentations.isEmpty())
        showNextReminderPresentation();
    else
        restorePageAfterReminder();
}

void AppController::expireCurrentReminderPresentation()
{
    if (!hasActiveReminderAlert())
        return;
    m_currentPresentation = {};
    m_window->clearReminderPresentation();
    if (!m_pendingPresentations.isEmpty())
        showNextReminderPresentation();
    else
        restorePageAfterReminder();
}

void AppController::restorePageAfterReminder()
{
    const MainWindow::PageId target = m_hasPageBeforeReminder
        ? m_pageBeforeReminder : MainWindow::PageId::Companion;
    m_hasPageBeforeReminder = false;
    showPage(target);
}

void AppController::showEmergency()
{
    if (m_emergencyActive)
        return;
    m_pageBeforeEmergency = m_window->currentPage();
    if (m_pageBeforeEmergency == MainWindow::PageId::Emergency)
        m_pageBeforeEmergency = MainWindow::PageId::Companion;
    m_emergencyActive = true;
    m_controlTimeout.stop();
    if (hasActiveReminderAlert())
        m_alertPresentationTimeout.stop();
    m_window->showPage(MainWindow::PageId::Emergency);
}

void AppController::dismissEmergency()
{
    if (!m_emergencyActive)
        return;
    m_emergencyActive = false;
    if (hasActiveReminderAlert()) {
        m_window->showPage(MainWindow::PageId::ReminderAlert);
        m_alertPresentationTimeout.start();
        return;
    }

    MainWindow::PageId target = m_pageBeforeEmergency;
    if (target == MainWindow::PageId::Emergency
        || target == MainWindow::PageId::ReminderAlert) {
        target = MainWindow::PageId::Companion;
    }
    m_window->showPage(target);
    if (!m_pendingPresentations.isEmpty())
        showNextReminderPresentation();
    else if (target != MainWindow::PageId::Companion)
        m_controlTimeout.start();
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
