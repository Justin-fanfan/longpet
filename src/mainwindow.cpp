#include "mainwindow.h"

#include "pages/CarePage.h"
#include "pages/CompanionPage.h"
#include "pages/EmergencyPage.h"
#include "pages/DeveloperPage.h"
#include "pages/HomePage.h"
#include "pages/ReminderEditPage.h"
#include "pages/ReminderAlertPage.h"
#include "pages/ReminderPage.h"
#include "pages/SettingsPage.h"
#include "widgets/VisualComponents.h"
#include "widgets/VisualTokens.h"

#include <QApplication>
#include <QEvent>
#include <QStackedWidget>
#include <QVBoxLayout>

MainWindow::MainWindow(bool developerMode, QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("mainWindow"));
    setProperty("page", true);
    setAttribute(Qt::WA_StyledBackground, true);
    setWindowTitle(QStringLiteral("LongPet"));
    setMinimumSize(LongPetUi::Metrics::CanvasWidth,
                   LongPetUi::Metrics::CanvasHeight);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_stack = new QStackedWidget(this);
    m_stack->setObjectName(QStringLiteral("pageStack"));
    m_companionPage = new CompanionPage(m_stack);
    m_homePage = new HomePage(m_stack);
    m_carePage = new CarePage(m_stack);
    m_reminderPage = new ReminderPage(m_stack);
    m_reminderEditPage = new ReminderEditPage(m_stack);
    m_settingsPage = new SettingsPage(m_stack);
    m_reminderAlertPage = new ReminderAlertPage(m_stack);
    m_emergencyPage = new EmergencyPage(m_stack);
    m_developerPage = new DeveloperPage(m_stack);
    for (QWidget* page : {static_cast<QWidget*>(m_companionPage),
                          static_cast<QWidget*>(m_homePage),
                          static_cast<QWidget*>(m_carePage),
                          static_cast<QWidget*>(m_reminderPage),
                          static_cast<QWidget*>(m_reminderEditPage),
                          static_cast<QWidget*>(m_settingsPage),
                          static_cast<QWidget*>(m_reminderAlertPage),
                          static_cast<QWidget*>(m_emergencyPage),
                          static_cast<QWidget*>(m_developerPage)}) {
        m_stack->addWidget(page);
    }
    root->addWidget(m_stack);

    m_toast = new ToastWidget(this);
    m_toast->setAccessibleName(QStringLiteral("功能提示"));
    connect(m_companionPage, &CompanionPage::controlRequested,
            this, &MainWindow::controlRequested);
    connect(m_homePage, &HomePage::talkRequested,
            this, &MainWindow::talkRequested);
    connect(m_homePage, &HomePage::careRequested,
            this, &MainWindow::careRequested);
    connect(m_homePage, &HomePage::reminderRequested,
            this, &MainWindow::reminderRequested);
    connect(m_homePage, &HomePage::settingsRequested,
            this, &MainWindow::settingsRequested);

    connect(m_carePage, &CarePage::backRequested,
            this, &MainWindow::homeRequested);
    connect(m_carePage, &CarePage::reminderRequested,
            this, &MainWindow::reminderRequested);
    connect(m_carePage, &CarePage::recordWaterRequested,
            this, &MainWindow::recordWaterRequested);
    connect(m_reminderPage, &ReminderPage::backRequested,
            this, &MainWindow::homeRequested);
    connect(m_reminderPage, &ReminderPage::addReminderRequested,
            this, &MainWindow::addReminderRequested);
    connect(m_reminderPage, &ReminderPage::editReminderRequested,
            this, &MainWindow::editReminderRequested);
    connect(m_reminderPage, &ReminderPage::completeReminderRequested,
            this, &MainWindow::completeReminderRequested);
    connect(m_reminderEditPage, &ReminderEditPage::backRequested,
            this, &MainWindow::cancelReminderEditRequested);
    connect(m_reminderEditPage, &ReminderEditPage::cancelRequested,
            this, &MainWindow::cancelReminderEditRequested);
    connect(m_reminderEditPage, &ReminderEditPage::saveRequested,
            this, &MainWindow::saveReminderRequested);
    connect(m_reminderEditPage, &ReminderEditPage::deleteRequested,
            this, &MainWindow::deleteReminderRequested);
    connect(m_reminderAlertPage, &ReminderAlertPage::acknowledgeRequested,
            this, &MainWindow::acknowledgeReminderAlertRequested);
    connect(m_reminderAlertPage, &ReminderAlertPage::completeRequested,
            this, &MainWindow::completeReminderAlertRequested);
    connect(m_settingsPage, &SettingsPage::backRequested,
            this, &MainWindow::homeRequested);
    connect(m_settingsPage, &SettingsPage::volumeChangeRequested,
            this, &MainWindow::volumeChangeRequested);
    connect(m_settingsPage, &SettingsPage::brightnessChangeRequested,
            this, &MainWindow::brightnessChangeRequested);
    connect(m_settingsPage, &SettingsPage::petStyleChangeRequested,
            this, &MainWindow::petStyleChangeRequested);
    connect(m_settingsPage, &SettingsPage::pairFamilyRequested,
            this, &MainWindow::pairFamilyRequested);
    m_settingsPage->setDeveloperMode(developerMode);
    connect(m_settingsPage, &SettingsPage::developerRequested,
            this, &MainWindow::developerRequested);
    connect(m_emergencyPage, &EmergencyPage::dismissRequested,
            this, &MainWindow::emergencyDismissRequested);
    connect(m_emergencyPage, &EmergencyPage::contactRequested,
            this, &MainWindow::emergencyContactRequested);
    connect(m_developerPage, &DeveloperPage::backRequested,
            this, &MainWindow::settingsRequested);
    connect(m_developerPage, &DeveloperPage::kwsEnableRequested,
            this, &MainWindow::kwsEnableRequested);
    connect(m_developerPage, &DeveloperPage::kwsStartRequested,
            this, &MainWindow::kwsStartRequested);
    connect(m_developerPage, &DeveloperPage::kwsStopRequested,
            this, &MainWindow::kwsStopRequested);
    connect(m_developerPage, &DeveloperPage::kwsRestartRequested,
            this, &MainWindow::kwsRestartRequested);
    connect(m_developerPage, &DeveloperPage::kwsReconfigureRequested,
            this, &MainWindow::kwsReconfigureRequested);
    connect(m_developerPage, &DeveloperPage::visionEnableRequested,
            this, &MainWindow::visionEnableRequested);
    connect(m_developerPage, &DeveloperPage::visionStartRequested,
            this, &MainWindow::visionStartRequested);
    connect(m_developerPage, &DeveloperPage::visionStopRequested,
            this, &MainWindow::visionStopRequested);
    connect(m_developerPage, &DeveloperPage::visionRestartRequested,
            this, &MainWindow::visionRestartRequested);
    connect(m_developerPage, &DeveloperPage::visionReconfigureRequested,
            this, &MainWindow::visionReconfigureRequested);
    connect(m_developerPage, &DeveloperPage::simulationRequested,
            this, &MainWindow::developerSimulationRequested);
    qApp->installEventFilter(this);
    showPage(PageId::Companion);
}

MainWindow::~MainWindow()
{
    if (qApp)
        qApp->removeEventFilter(this);
}

MainWindow::PageId MainWindow::currentPage() const
{
    return m_currentPage;
}

void MainWindow::showPage(PageId page)
{
    QWidget* target = pageWidget(page);
    if (!target)
        return;
    m_toast->hide();
    m_stack->setCurrentWidget(target);
    m_currentPage = page;
}

void MainWindow::showToast(const QString& text)
{
    m_toast->showMessage(text);
}

void MainWindow::setReminders(const QList<Reminder>& reminders)
{
    m_reminderPage->setReminders(reminders);
}

void MainWindow::setReminderDraft(const ReminderDraft& draft)
{
    m_reminderEditPage->setDraft(draft);
}

void MainWindow::setReminderPresentation(const ReminderPresentation& presentation)
{
    m_reminderAlertPage->setPresentation(presentation);
}

void MainWindow::clearReminderPresentation()
{
    m_reminderAlertPage->clearPresentation();
}

void MainWindow::setCareSummary(const CareSummary& summary)
{
    m_carePage->setSummary(summary);
}

void MainWindow::setSettings(const UserSettings& settings)
{
    m_settingsPage->setSettings(settings);
}

void MainWindow::setSystemStatus(const SystemStatus& status)
{
    const QList<StatusBarWidget*> statusBars = findChildren<StatusBarWidget*>();
    for (StatusBarWidget* statusBar : statusBars)
        statusBar->setStatus(status);
}

void MainWindow::setDeviceSummary(const DeviceSummary& summary)
{
    m_settingsPage->setDeviceSummary(summary);
}

void MainWindow::setEmergencyDetail(const QString& detail)
{
    m_emergencyPage->setDetail(detail);
}

void MainWindow::setDeveloperSnapshot(const DeveloperSnapshot& snapshot)
{
    m_developerPage->setSnapshot(snapshot);
}

void MainWindow::setDiagnosticEvents(const QList<DiagnosticEvent>& events)
{
    m_developerPage->setEvents(events);
}

void MainWindow::appendDiagnosticEvent(const DiagnosticEvent& event)
{
    m_developerPage->appendEvent(event);
}

void MainWindow::setDeveloperAudioLevel(double rms, double peak)
{
    m_developerPage->setAudioLevel(rms, peak);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    const auto* watchedWidget = qobject_cast<QWidget*>(watched);
    QWidget* current = pageWidget(m_currentPage);
    const bool belongsToCurrent = watched == current
        || (watchedWidget && current && current->isAncestorOf(watchedWidget));
    if (belongsToCurrent && m_currentPage != PageId::Companion
        && m_currentPage != PageId::ReminderAlert
        && m_currentPage != PageId::Developer) {
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::TouchBegin:
        case QEvent::KeyPress:
            emit userActivity(m_currentPage);
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

QWidget* MainWindow::pageWidget(PageId page) const
{
    switch (page) {
    case PageId::Companion: return m_companionPage;
    case PageId::Home: return m_homePage;
    case PageId::Care: return m_carePage;
    case PageId::Reminder: return m_reminderPage;
    case PageId::ReminderEdit: return m_reminderEditPage;
    case PageId::Settings: return m_settingsPage;
    case PageId::ReminderAlert: return m_reminderAlertPage;
    case PageId::Emergency: return m_emergencyPage;
    case PageId::Developer: return m_developerPage;
    }
    return nullptr;
}
