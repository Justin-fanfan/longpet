#include "Application.h"

#include "AppController.h"
#include "FamilyLinkController.h"
#include "data/CareEventRepository.h"
#include "data/AiConfigRepository.h"
#include "data/DatabaseManager.h"
#include "data/ReminderRepository.h"
#include "data/SettingsRepository.h"
#include "mainwindow.h"
#include "platform/AudioVolumeAdapter.h"
#include "platform/BacklightAdapter.h"
#include "platform/CameraCaptureAdapter.h"
#include "platform/CallPromptPlayerAdapter.h"
#include "platform/FamilyLinkHttpAdapter.h"
#include "platform/VisionDetectorFactory.h"
#include "platform/NetworkStatusAdapter.h"
#include "platform/NetworkManagerAdapter.h"
#include "platform/AiProviderFactory.h"
#include "platform/PowerStatusAdapter.h"
#include "platform/VideoCallMediaAdapter.h"
#include "platform/VoiceAudioAdapter.h"
#include "platform/KwsProcessAdapter.h"
#include "platform/OfflineAudioLibraryAdapter.h"
#include "platform/WeatherProviderFactory.h"
#include "data/WeatherConfigRepository.h"
#include "services/CareService.h"
#include "services/FamilyLinkService.h"
#include "services/ReminderService.h"
#include "services/NetworkService.h"
#include "services/MediaSessionCoordinator.h"
#include "services/SettingsService.h"
#include "services/SystemService.h"
#include "services/VideoCallService.h"
#include "services/VisionService.h"
#include "services/VisionPorts.h"
#include "services/VoiceInteractionService.h"
#include "services/VoiceInteractionPorts.h"
#include "services/VoiceToolRegistry.h"
#include "services/LocalCompanionService.h"
#include "services/VoiceCapabilityService.h"
#include "services/VoiceCommandDispatcher.h"
#include "services/WeatherService.h"
#include "services/WeatherPorts.h"
#include "widgets/VisualTokens.h"

#include <QCoreApplication>
#include <QDir>
#include <QHostAddress>
#include <QStandardPaths>

namespace {
quint16 configuredFamilyLinkPort()
{
    bool valid = false;
    const int configured = qEnvironmentVariableIntValue("LONGPET_FAMILY_LINK_PORT", &valid);
    if (!valid || configured < 0 || configured > 65'535)
        return 8'787;
    return static_cast<quint16>(configured);
}

QHostAddress configuredFamilyLinkAddress()
{
    const QString configured = qEnvironmentVariable("LONGPET_FAMILY_LINK_ADDRESS").trimmed();
    QHostAddress address;
    if (!configured.isEmpty() && address.setAddress(configured))
        return address;
    return QHostAddress(QHostAddress::LocalHost);
}

bool configuredVisionEnabled()
{
    const QString value = qEnvironmentVariable("LONGPET_VISION_ENABLED")
                              .trimmed().toLower();
    return value == QStringLiteral("1")
        || value == QStringLiteral("true")
        || value == QStringLiteral("yes")
        || value == QStringLiteral("on");
}
}

Application::Application(QObject* parent)
    : QObject(parent)
{
}

Application::~Application()
{
    shutdown();
}

bool Application::initialize(QString* error)
{
    m_database = std::make_unique<DatabaseManager>();
    if (!m_database->open(resolveDatabasePath(), error))
        return false;

    m_reminderRepository = std::make_unique<ReminderRepository>(m_database->database());
    m_careEventRepository = std::make_unique<CareEventRepository>(m_database->database());
    m_settingsRepository = std::make_unique<SettingsRepository>(m_database->database());
    m_aiConfigRepository = std::make_unique<AiConfigRepository>(resolveAiConfigPath());
    m_reminderService = std::make_unique<ReminderService>(m_reminderRepository.get());
    m_careService = std::make_unique<CareService>(m_careEventRepository.get(),
                                                  m_reminderService.get());
    m_settingsService = std::make_unique<SettingsService>(m_settingsRepository.get());
    m_systemService = std::make_unique<SystemService>();
    m_mediaSessionCoordinator = std::make_unique<MediaSessionCoordinator>();
    QString aiConfigurationError;
    const AiConfiguration aiConfiguration = m_aiConfigRepository->load(
        &aiConfigurationError);
    if (!aiConfigurationError.isEmpty())
        qWarning().noquote() << aiConfigurationError;
    m_asrProvider = AiProviderFactory::createAsr(
        aiConfiguration.asr, aiConfiguration.voice.requestTimeoutMs);
    m_llmProvider = AiProviderFactory::createLlm(
        aiConfiguration.llm, aiConfiguration.voice.requestTimeoutMs);
    m_ttsProvider = AiProviderFactory::createTts(
        aiConfiguration.tts, aiConfiguration.voice.requestTimeoutMs);
    m_voiceAudioAdapter = std::make_unique<VoiceAudioAdapter>();
    m_weatherConfigRepository = std::make_unique<WeatherConfigRepository>(
        resolveWeatherConfigPath());
    QString weatherConfigError;
    const WeatherConfiguration weatherConfiguration =
        m_weatherConfigRepository->load(&weatherConfigError);
    if (!weatherConfigError.isEmpty())
        qWarning().noquote() << weatherConfigError;
    m_weatherProvider = WeatherProviderFactory::create(weatherConfiguration);
    m_weatherService = std::make_unique<WeatherService>(
        weatherConfiguration, m_weatherProvider.get(), m_systemService.get());
    m_voiceInteractionService = std::make_unique<VoiceInteractionService>(
        aiConfiguration, m_asrProvider.get(), m_llmProvider.get(),
        m_ttsProvider.get(), m_voiceAudioAdapter.get(),
        m_mediaSessionCoordinator.get());
    m_voiceToolRegistry = std::make_unique<VoiceToolRegistry>(
        m_reminderService.get());
    m_voiceInteractionService->setToolRegistry(
        aiConfiguration.tools.enabled
            && aiConfiguration.tools.validationError().isEmpty()
            ? m_voiceToolRegistry.get() : nullptr);
    // 给语音交互注入一个只读天气入口；VoiceInteractionService 不接触 Provider。
    m_voiceInteractionService->setWeatherProvider(
        [weatherService = m_weatherService.get()]() {
            return weatherService ? weatherService->currentOrNone()
                                  : std::optional<WeatherSnapshot>{};
        });
    m_cameraCaptureAdapter = std::make_unique<CameraCaptureAdapter>();
    m_visionDetector = VisionDetectorFactory::createFromEnvironment();
    m_visionService = std::make_unique<VisionService>(
        m_cameraCaptureAdapter.get(), m_visionDetector.get());
    m_videoCallMediaAdapter = std::make_unique<VideoCallMediaAdapter>(
        m_cameraCaptureAdapter.get());
    m_callPromptPlayerAdapter = std::make_unique<CallPromptPlayerAdapter>();
    m_videoCallService = std::make_unique<VideoCallService>(
        m_videoCallMediaAdapter.get(), m_callPromptPlayerAdapter.get(),
        m_mediaSessionCoordinator.get());
    connect(m_videoCallService.get(), &VideoCallService::callActivityChanged,
            m_visionService.get(), &VisionService::setVideoCallActive);
    connect(m_visionService.get(), &VisionService::visionResultReady, this,
            [](const VisionFrameResult& result) {
        qInfo().noquote()
            << QStringLiteral(
                   "Vision frame=%1 persons=%2 decode_ms=%3 preprocess_ms=%4 "
                   "inference_ms=%5 postprocess_ms=%6 total_ms=%7")
                   .arg(result.frameSequence)
                   .arg(result.persons.size())
                   .arg(result.decodeMs, 0, 'f', 2)
                   .arg(result.preprocessMs, 0, 'f', 2)
                   .arg(result.inferenceMs, 0, 'f', 2)
                   .arg(result.postprocessMs, 0, 'f', 2)
                   .arg(result.totalMs, 0, 'f', 2);
    });
    connect(m_visionService.get(), &VisionService::failed, this,
            [](const QString& stage, const QString& message) {
        qWarning().noquote() << QStringLiteral("Vision %1 unavailable: %2")
                                    .arg(stage, message);
    });
    if (configuredVisionEnabled())
        m_visionService->start();
    m_networkStatusAdapter = std::make_unique<NetworkStatusAdapter>();
    m_networkManagerAdapter = std::make_unique<NetworkManagerAdapter>();
    m_networkService = std::make_unique<NetworkService>(m_networkManagerAdapter.get());
    m_audioVolumeAdapter = std::make_unique<AudioVolumeAdapter>();
    m_backlightAdapter = std::make_unique<BacklightAdapter>();
    m_powerStatusAdapter = std::make_unique<PowerStatusAdapter>();
    m_offlineAudioLibraryAdapter = std::make_unique<OfflineAudioLibraryAdapter>(
        aiConfiguration.offline.companionAudioDirectory);
    m_localCompanionService = std::make_unique<LocalCompanionService>(
        aiConfiguration.offline, m_offlineAudioLibraryAdapter.get(),
        m_voiceAudioAdapter.get(), m_mediaSessionCoordinator.get());
    m_voiceCapabilityService = std::make_unique<VoiceCapabilityService>(
        aiConfiguration, m_systemService.get());
    connect(m_voiceInteractionService.get(),
            &VoiceInteractionService::providerAvailabilityChanged,
            m_voiceCapabilityService.get(),
            &VoiceCapabilityService::reportProviderAvailability);
    m_kwsProcessAdapter = std::make_unique<KwsProcessAdapter>(
        aiConfiguration.kws);
    m_voiceCommandDispatcher = std::make_unique<VoiceCommandDispatcher>(
        aiConfiguration.kws, m_kwsProcessAdapter.get(),
        m_voiceCapabilityService.get(), m_voiceInteractionService.get(),
        m_localCompanionService.get());
    connect(m_networkStatusAdapter.get(), &NetworkStatusAdapter::networkStateChanged,
            m_systemService.get(), &SystemService::setNetworkState);
    connect(m_audioVolumeAdapter.get(), &AudioVolumeAdapter::controlStateChanged,
            m_systemService.get(), &SystemService::setAudioControlState);
    connect(m_backlightAdapter.get(), &BacklightAdapter::controlStateChanged,
            m_systemService.get(), &SystemService::setBacklightControlState);
    connect(m_powerStatusAdapter.get(), &PowerStatusAdapter::batteryPercentChanged,
            m_systemService.get(), &SystemService::setBatteryPercent);
    connect(m_powerStatusAdapter.get(), &PowerStatusAdapter::powerStateChanged,
            m_systemService.get(), &SystemService::setPowerSummary);
    connect(m_settingsService.get(), &SettingsService::settingApplyRequested,
            this, [this](const QString& key, const QVariant& value) {
        if (key == QStringLiteral("volume"))
            m_audioVolumeAdapter->applyVolume(value.toInt());
        else if (key == QStringLiteral("brightness"))
            m_backlightAdapter->applyBrightness(value.toInt());
    });
    m_networkStatusAdapter->start();
    m_audioVolumeAdapter->start();
    m_backlightAdapter->start();
    m_powerStatusAdapter->start();
    m_weatherService->start();
    const UserSettings currentSettings = m_settingsService->settings();
    m_audioVolumeAdapter->applyVolume(currentSettings.volume);
    m_backlightAdapter->applyBrightness(currentSettings.brightness);
    m_familyLinkService = std::make_unique<FamilyLinkService>(
        m_reminderService.get(), m_careService.get(), m_settingsService.get(),
        m_systemService.get(), m_videoCallService.get());
    m_familyLinkHttpAdapter = std::make_unique<FamilyLinkHttpAdapter>();
    const QByteArray familyLinkToken = qEnvironmentVariable("LONGPET_FAMILY_LINK_TOKEN").toUtf8();
    const QHostAddress familyLinkAddress = configuredFamilyLinkAddress();
    m_familyLinkController = std::make_unique<FamilyLinkController>(
        m_familyLinkService.get(), m_familyLinkHttpAdapter.get(), familyLinkToken);
    QString familyLinkError;
    const bool remoteAddress = familyLinkAddress != QHostAddress(QHostAddress::LocalHost)
        && familyLinkAddress != QHostAddress(QHostAddress::LocalHostIPv6);
    if (remoteAddress && familyLinkToken.isEmpty()) {
        qWarning("FamilyLink remote listener disabled: LONGPET_FAMILY_LINK_TOKEN is required");
    } else if (!m_familyLinkController->start(configuredFamilyLinkPort(), familyLinkAddress,
                                               &familyLinkError)) {
        qWarning("FamilyLink HTTP service unavailable: %s", qPrintable(familyLinkError));
    } else {
        qInfo("FamilyLink API listening on %s:%u",
              qPrintable(familyLinkAddress.toString()),
              static_cast<unsigned>(m_familyLinkController->port()));
    }
    m_window = std::make_unique<MainWindow>();
    m_controller = std::make_unique<AppController>(m_window.get(),
        m_reminderService.get(), m_careService.get(), m_settingsService.get(),
        m_systemService.get(), 15'000, m_networkService.get(),
        m_videoCallService.get(), m_voiceInteractionService.get(),
        m_voiceCommandDispatcher.get(), m_voiceToolRegistry.get());
    m_controller->initialize();
    m_voiceCommandDispatcher->start();
    return true;
}

void Application::show()
{
    if (!m_window)
        return;
#ifdef Q_OS_WIN
    m_window->setFixedSize(LongPetUi::Metrics::CanvasWidth,
                           LongPetUi::Metrics::CanvasHeight);
    m_window->show();
#else
    m_window->showFullScreen();
#endif
}

void Application::shutdown()
{
    if (m_voiceCommandDispatcher)
        m_voiceCommandDispatcher->stop();
    if (m_weatherService)
        m_weatherService->stop();
    if (m_visionService)
        m_visionService->stop();
    if (m_familyLinkController)
        m_familyLinkController->stop();
    if (m_reminderService)
        m_reminderService->stop();
    if (m_networkStatusAdapter)
        m_networkStatusAdapter->stop();
    if (m_audioVolumeAdapter)
        m_audioVolumeAdapter->stop();
    if (m_backlightAdapter)
        m_backlightAdapter->stop();
    if (m_powerStatusAdapter)
        m_powerStatusAdapter->stop();
    m_controller.reset();
    m_window.reset();
    m_familyLinkController.reset();
    m_familyLinkHttpAdapter.reset();
    m_familyLinkService.reset();
    m_networkService.reset();
    m_networkManagerAdapter.reset();
    m_powerStatusAdapter.reset();
    m_backlightAdapter.reset();
    m_audioVolumeAdapter.reset();
    m_networkStatusAdapter.reset();
    m_voiceCommandDispatcher.reset();
    m_kwsProcessAdapter.reset();
    m_voiceCapabilityService.reset();
    m_localCompanionService.reset();
    m_offlineAudioLibraryAdapter.reset();
    m_voiceToolRegistry.reset();
    m_voiceInteractionService.reset();
    m_weatherService.reset();
    m_weatherProvider.reset();
    m_weatherConfigRepository.reset();
    m_voiceAudioAdapter.reset();
    m_ttsProvider.reset();
    m_llmProvider.reset();
    m_asrProvider.reset();
    m_videoCallService.reset();
    m_callPromptPlayerAdapter.reset();
    m_videoCallMediaAdapter.reset();
    m_visionService.reset();
    m_visionDetector.reset();
    m_cameraCaptureAdapter.reset();
    m_systemService.reset();
    m_mediaSessionCoordinator.reset();
    m_settingsService.reset();
    m_careService.reset();
    m_reminderService.reset();
    m_settingsRepository.reset();
    m_aiConfigRepository.reset();
    m_careEventRepository.reset();
    m_reminderRepository.reset();
    if (m_database)
        m_database->close();
    m_database.reset();
}

MainWindow* Application::window() const
{
    return m_window.get();
}

QString Application::databasePath() const
{
    return m_database ? m_database->databasePath() : QString();
}

QString Application::resolveDatabasePath() const
{
    const QString overridePath = qEnvironmentVariable("LONGPET_DATABASE_PATH");
    if (!overridePath.isEmpty())
        return overridePath;
    const QString dataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(dataDirectory).filePath(QStringLiteral("longpet.db"));
}

QString Application::resolveAiConfigPath() const
{
    const QString overridePath = qEnvironmentVariable("LONGPET_AI_CONFIG").trimmed();
    if (!overridePath.isEmpty())
        return overridePath;
#ifdef Q_OS_LINUX
    return QStringLiteral("/etc/longpet/ai.ini");
#else
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
        .filePath(QStringLiteral("longpet-ai.ini"));
#endif
}

QString Application::resolveWeatherConfigPath() const
{
    const QString overridePath = qEnvironmentVariable("LONGPET_WEATHER_CONFIG").trimmed();
    if (!overridePath.isEmpty())
        return overridePath;
#ifdef Q_OS_LINUX
    return QStringLiteral("/etc/longpet/longpet-weather.ini");
#else
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
        .filePath(QStringLiteral("longpet-weather.ini"));
#endif
}
