#include "Application.h"

#include "AppController.h"
#include "data/CareEventRepository.h"
#include "data/DatabaseManager.h"
#include "data/ReminderRepository.h"
#include "data/SettingsRepository.h"
#include "mainwindow.h"
#include "platform/AudioVolumeAdapter.h"
#include "platform/BacklightAdapter.h"
#include "platform/KeywordSpottingAdapter.h"
#include "platform/NetworkStatusAdapter.h"
#include "platform/PowerStatusAdapter.h"
#include "platform/VisionAdapter.h"
#include "services/CareService.h"
#include "services/KeywordSpottingService.h"
#include "services/ReminderService.h"
#include "services/SettingsService.h"
#include "services/SystemService.h"
#include "services/VisionService.h"
#include "widgets/VisualTokens.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QTimer>

namespace {
constexpr int KeywordSpottingStartupDelayMs = 1'500;
// The board KWS runtime needs roughly 25 seconds to load its model. Starting
// OpenCV during that cold-start peak increases both latency and memory
// pressure, so vision is intentionally staggered behind it.
constexpr int VisionStartupDelayMs = 30'000;
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
    m_reminderService = std::make_unique<ReminderService>(m_reminderRepository.get());
    m_careService = std::make_unique<CareService>(m_careEventRepository.get(),
                                                  m_reminderService.get());
    m_settingsService = std::make_unique<SettingsService>(m_settingsRepository.get());
    m_systemService = std::make_unique<SystemService>();
    m_keywordSpottingAdapter = std::make_unique<KeywordSpottingAdapter>();
    m_keywordSpottingService = std::make_unique<KeywordSpottingService>(
        m_keywordSpottingAdapter.get());
    m_visionAdapter = std::make_unique<VisionAdapter>();
    m_visionService = std::make_unique<VisionService>(m_visionAdapter.get());
    m_networkStatusAdapter = std::make_unique<NetworkStatusAdapter>();
    m_audioVolumeAdapter = std::make_unique<AudioVolumeAdapter>();
    m_backlightAdapter = std::make_unique<BacklightAdapter>();
    m_powerStatusAdapter = std::make_unique<PowerStatusAdapter>();
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
    connect(m_keywordSpottingService.get(), &KeywordSpottingService::statusChanged,
            this, [this](const KeywordSpottingStatus& status) {
        m_systemService->setKeywordSpottingState(
            status.available, status.listening, status.summary,
            status.lastKeyword);
    });
    connect(m_visionService.get(), &VisionService::statusChanged,
            this, [this](const VisionStatus& status) {
        m_systemService->setVisionState(
            status.available, status.monitoring, status.summary,
            status.effectiveFps);
    });
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
    const UserSettings currentSettings = m_settingsService->settings();
    m_audioVolumeAdapter->applyVolume(currentSettings.volume);
    m_backlightAdapter->applyBrightness(currentSettings.brightness);
    m_window = std::make_unique<MainWindow>();
    m_controller = std::make_unique<AppController>(m_window.get(),
        m_reminderService.get(), m_careService.get(), m_settingsService.get(),
        m_systemService.get(), 15'000, m_keywordSpottingService.get(),
        m_visionService.get());
    m_controller->initialize();
    const KeywordSpottingStatus keywordStatus = m_keywordSpottingService->status();
    m_systemService->setKeywordSpottingState(
        keywordStatus.available, keywordStatus.listening,
        keywordStatus.summary, keywordStatus.lastKeyword);
    const VisionStatus visionStatus = m_visionService->status();
    m_systemService->setVisionState(
        visionStatus.available, visionStatus.monitoring,
        visionStatus.summary, visionStatus.effectiveFps);
    QTimer::singleShot(KeywordSpottingStartupDelayMs, this, [this] {
        if (m_keywordSpottingAdapter)
            m_keywordSpottingAdapter->start();
    });
    QTimer::singleShot(VisionStartupDelayMs, this, [this] {
        if (m_visionAdapter)
            m_visionAdapter->start();
    });
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
    if (m_visionAdapter)
        m_visionAdapter->stop();
    if (m_keywordSpottingAdapter)
        m_keywordSpottingAdapter->stop();
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
    m_powerStatusAdapter.reset();
    m_backlightAdapter.reset();
    m_audioVolumeAdapter.reset();
    m_networkStatusAdapter.reset();
    m_visionService.reset();
    m_visionAdapter.reset();
    m_keywordSpottingService.reset();
    m_keywordSpottingAdapter.reset();
    m_systemService.reset();
    m_settingsService.reset();
    m_careService.reset();
    m_reminderService.reset();
    m_settingsRepository.reset();
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
