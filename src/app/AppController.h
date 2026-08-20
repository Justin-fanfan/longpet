#pragma once

#include "mainwindow.h"
#include "model/KeywordSpottingModels.h"
#include "model/ReminderModels.h"

#include <QObject>
#include <QQueue>
#include <QSet>
#include <QTimer>

class CareService;
class DeveloperService;
class KeywordSpottingService;
class ReminderService;
class SettingsService;
class SystemService;
class VisionService;

class AppController final : public QObject {
    Q_OBJECT

public:
    AppController(MainWindow* window,
                  ReminderService* reminderService,
                  CareService* careService,
                  SettingsService* settingsService,
                  SystemService* systemService,
                  int controlTimeoutMs = 15'000,
                  KeywordSpottingService* keywordSpottingService = nullptr,
                  VisionService* visionService = nullptr,
                  DeveloperService* developerService = nullptr,
                  QObject* parent = nullptr);

    void initialize();

    bool hasActiveReminderAlert() const;
    ReminderEventId currentReminderEventId() const;
    bool handleReminderConfirmation(ReminderConfirmationSemantic semantic,
                                    ReminderAckSource source = ReminderAckSource::Voice);
    bool handleKeywordSemantic(KeywordSemantic semantic,
                               const QString& keyword = {});

signals:
    void reminderVoicePlaybackRequested(const Reminder& reminder);
    void stopVoicePlaybackRequested();

private:
    void connectUi();
    void connectServices();
    void showHome();
    void showPage(MainWindow::PageId page);
    void showCare();
    void showReminders();
    void showSettings();
    void showDeveloper();
    void editReminder(ReminderId id);
    void saveReminder(const ReminderDraft& draft);
    void deleteReminder(ReminderId id);
    void completeReminder(ReminderId id);
    void enqueueReminderPresentation(const ReminderPresentation& presentation);
    void showNextReminderPresentation();
    void finishCurrentReminderPresentation();
    void expireCurrentReminderPresentation();
    void restorePageAfterReminder();
    void showEmergency(const QString& detail = {});
    void dismissEmergency();
    void refreshReminders();
    void refreshCare();
    void refreshSettings();
    void showDataError(const QString& context, const QString& error);

    MainWindow* m_window = nullptr;
    ReminderService* m_reminderService = nullptr;
    CareService* m_careService = nullptr;
    SettingsService* m_settingsService = nullptr;
    SystemService* m_systemService = nullptr;
    KeywordSpottingService* m_keywordSpottingService = nullptr;
    VisionService* m_visionService = nullptr;
    DeveloperService* m_developerService = nullptr;
    QTimer m_controlTimeout;
    QTimer m_alertPresentationTimeout;
    QQueue<ReminderPresentation> m_pendingPresentations;
    QSet<ReminderEventId> m_queuedEventIds;
    ReminderPresentation m_currentPresentation;
    MainWindow::PageId m_pageBeforeReminder = MainWindow::PageId::Companion;
    bool m_hasPageBeforeReminder = false;
    MainWindow::PageId m_pageBeforeEmergency = MainWindow::PageId::Companion;
    bool m_emergencyActive = false;
};
