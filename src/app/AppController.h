#pragma once

#include "mainwindow.h"
#include "model/ReminderModels.h"

#include <QObject>
#include <QTimer>

class CareService;
class NetworkService;
class ReminderService;
class SettingsService;
class SystemService;
class VideoCallService;
class VoiceInteractionService;
class VoiceCommandDispatcher;
class VoiceToolRegistry;

class AppController final : public QObject {
    Q_OBJECT

public:
    AppController(MainWindow* window,
                  ReminderService* reminderService,
                  CareService* careService,
                  SettingsService* settingsService,
                  SystemService* systemService,
                  int controlTimeoutMs = 15'000,
                  NetworkService* networkService = nullptr,
                  VideoCallService* videoCallService = nullptr,
                  VoiceInteractionService* voiceInteractionService = nullptr,
                  VoiceCommandDispatcher* voiceCommandDispatcher = nullptr,
                  VoiceToolRegistry* voiceToolRegistry = nullptr,
                  QObject* parent = nullptr);

    void initialize();

private:
    void connectUi();
    void connectServices();
    void showHome();
    void showPage(MainWindow::PageId page);
    void showCare();
    void showReminders();
    void showSettings();
    void showNetworkSetup();
    void showVideoCall();
    void showEmergency();
    void showToolPage(const QString& page);
    void hangUpVideoCall();
    void startVoiceInteraction();
    void handleVoicePrimary();
    void handleVoiceSecondary();
    void editReminder(ReminderId id);
    void saveReminder(const ReminderDraft& draft);
    void deleteReminder(ReminderId id);
    void completeReminder(ReminderId id);
    void refreshReminders();
    void refreshCare();
    void refreshSettings();
    void showDataError(const QString& context, const QString& error);

    MainWindow* m_window = nullptr;
    ReminderService* m_reminderService = nullptr;
    CareService* m_careService = nullptr;
    SettingsService* m_settingsService = nullptr;
    SystemService* m_systemService = nullptr;
    NetworkService* m_networkService = nullptr;
    VideoCallService* m_videoCallService = nullptr;
    VoiceInteractionService* m_voiceInteractionService = nullptr;
    VoiceCommandDispatcher* m_voiceCommandDispatcher = nullptr;
    VoiceToolRegistry* m_voiceToolRegistry = nullptr;
    QTimer m_controlTimeout;
    QString m_pendingVoiceToolPage;
};
