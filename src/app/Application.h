#pragma once

#include <QObject>
#include <QString>

#include <memory>

class AppController;
class AudioVolumeAdapter;
class BacklightAdapter;
class CareEventRepository;
class CareService;
class DatabaseManager;
class KeywordSpottingAdapter;
class KeywordSpottingService;
class MainWindow;
class NetworkStatusAdapter;
class PowerStatusAdapter;
class ReminderRepository;
class ReminderService;
class SettingsRepository;
class SettingsService;
class SystemService;

class Application final : public QObject {
    Q_OBJECT

public:
    explicit Application(QObject* parent = nullptr);
    ~Application() override;

    bool initialize(QString* error = nullptr);
    void show();
    void shutdown();

    MainWindow* window() const;
    QString databasePath() const;

private:
    QString resolveDatabasePath() const;

    std::unique_ptr<DatabaseManager> m_database;
    std::unique_ptr<ReminderRepository> m_reminderRepository;
    std::unique_ptr<CareEventRepository> m_careEventRepository;
    std::unique_ptr<SettingsRepository> m_settingsRepository;
    std::unique_ptr<ReminderService> m_reminderService;
    std::unique_ptr<CareService> m_careService;
    std::unique_ptr<SettingsService> m_settingsService;
    std::unique_ptr<SystemService> m_systemService;
    std::unique_ptr<KeywordSpottingAdapter> m_keywordSpottingAdapter;
    std::unique_ptr<KeywordSpottingService> m_keywordSpottingService;
    std::unique_ptr<NetworkStatusAdapter> m_networkStatusAdapter;
    std::unique_ptr<AudioVolumeAdapter> m_audioVolumeAdapter;
    std::unique_ptr<BacklightAdapter> m_backlightAdapter;
    std::unique_ptr<PowerStatusAdapter> m_powerStatusAdapter;
    std::unique_ptr<MainWindow> m_window;
    std::unique_ptr<AppController> m_controller;
};
