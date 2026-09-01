#pragma once

#include <QObject>
#include <QString>

#include <memory>

class AppController;
class AiConfigRepository;
class AsrProviderPort;
class AudioVolumeAdapter;
class BacklightAdapter;
class CareEventRepository;
class CareService;
class CameraCaptureAdapter;
class DatabaseManager;
class FamilyLinkController;
class FamilyLinkHttpAdapter;
class FamilyLinkService;
class MainWindow;
class MediaSessionCoordinator;
class NetworkStatusAdapter;
class NetworkManagerAdapter;
class NetworkService;
class PowerStatusAdapter;
class LlmProviderPort;
class ReminderRepository;
class ReminderService;
class SettingsRepository;
class SettingsService;
class SystemService;
class VideoCallService;
class VideoCallMediaAdapter;
class VisionService;
class VisionDetectorPort;
class CallPromptPlayerAdapter;
class VoiceAudioAdapter;
class VoiceInteractionService;
class VoiceCommandDispatcher;
class VoiceCapabilityService;
class VoiceToolRegistry;
class LocalCompanionService;
class OfflineAudioLibraryAdapter;
class KwsProcessAdapter;
class WeatherConfigRepository;
class WeatherProviderPort;
class WeatherService;
class TtsProviderPort;

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
    QString resolveAiConfigPath() const;
    QString resolveWeatherConfigPath() const;

    std::unique_ptr<DatabaseManager> m_database;
    std::unique_ptr<ReminderRepository> m_reminderRepository;
    std::unique_ptr<CareEventRepository> m_careEventRepository;
    std::unique_ptr<SettingsRepository> m_settingsRepository;
    std::unique_ptr<AiConfigRepository> m_aiConfigRepository;
    std::unique_ptr<ReminderService> m_reminderService;
    std::unique_ptr<CareService> m_careService;
    std::unique_ptr<SettingsService> m_settingsService;
    std::unique_ptr<SystemService> m_systemService;
    std::unique_ptr<MediaSessionCoordinator> m_mediaSessionCoordinator;
    std::unique_ptr<AsrProviderPort> m_asrProvider;
    std::unique_ptr<LlmProviderPort> m_llmProvider;
    std::unique_ptr<TtsProviderPort> m_ttsProvider;
    std::unique_ptr<VoiceAudioAdapter> m_voiceAudioAdapter;
    std::unique_ptr<VoiceInteractionService> m_voiceInteractionService;
    std::unique_ptr<VoiceToolRegistry> m_voiceToolRegistry;
    std::unique_ptr<OfflineAudioLibraryAdapter> m_offlineAudioLibraryAdapter;
    std::unique_ptr<LocalCompanionService> m_localCompanionService;
    std::unique_ptr<VoiceCapabilityService> m_voiceCapabilityService;
    std::unique_ptr<KwsProcessAdapter> m_kwsProcessAdapter;
    std::unique_ptr<VoiceCommandDispatcher> m_voiceCommandDispatcher;
    std::unique_ptr<WeatherConfigRepository> m_weatherConfigRepository;
    std::unique_ptr<WeatherProviderPort> m_weatherProvider;
    std::unique_ptr<WeatherService> m_weatherService;
    std::unique_ptr<CameraCaptureAdapter> m_cameraCaptureAdapter;
    std::unique_ptr<VisionDetectorPort> m_visionDetector;
    std::unique_ptr<VisionService> m_visionService;
    std::unique_ptr<VideoCallMediaAdapter> m_videoCallMediaAdapter;
    std::unique_ptr<CallPromptPlayerAdapter> m_callPromptPlayerAdapter;
    std::unique_ptr<VideoCallService> m_videoCallService;
    std::unique_ptr<NetworkStatusAdapter> m_networkStatusAdapter;
    std::unique_ptr<NetworkManagerAdapter> m_networkManagerAdapter;
    std::unique_ptr<NetworkService> m_networkService;
    std::unique_ptr<AudioVolumeAdapter> m_audioVolumeAdapter;
    std::unique_ptr<BacklightAdapter> m_backlightAdapter;
    std::unique_ptr<PowerStatusAdapter> m_powerStatusAdapter;
    std::unique_ptr<FamilyLinkService> m_familyLinkService;
    std::unique_ptr<FamilyLinkHttpAdapter> m_familyLinkHttpAdapter;
    std::unique_ptr<FamilyLinkController> m_familyLinkController;
    std::unique_ptr<MainWindow> m_window;
    std::unique_ptr<AppController> m_controller;
};
