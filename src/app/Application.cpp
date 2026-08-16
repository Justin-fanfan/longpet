#include "Application.h"

#include "AppController.h"
#include "data/CareEventRepository.h"
#include "data/DatabaseManager.h"
#include "data/ReminderRepository.h"
#include "data/SettingsRepository.h"
#include "mainwindow.h"
#include "platform/NetworkStatusAdapter.h"
#include "services/CareService.h"
#include "services/ReminderService.h"
#include "services/SettingsService.h"
#include "services/SystemService.h"
#include "widgets/VisualTokens.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

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
    m_networkStatusAdapter = std::make_unique<NetworkStatusAdapter>();
    connect(m_networkStatusAdapter.get(), &NetworkStatusAdapter::networkStateChanged,
            m_systemService.get(), &SystemService::setNetworkState);
    m_networkStatusAdapter->start();
    m_window = std::make_unique<MainWindow>();
    m_controller = std::make_unique<AppController>(m_window.get(),
        m_reminderService.get(), m_careService.get(), m_settingsService.get(),
        m_systemService.get(), 15'000);
    m_controller->initialize();
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
    if (m_reminderService)
        m_reminderService->stop();
    if (m_networkStatusAdapter)
        m_networkStatusAdapter->stop();
    m_controller.reset();
    m_window.reset();
    m_networkStatusAdapter.reset();
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
