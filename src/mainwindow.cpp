#include "mainwindow.h"

#include "pages/CompanionPage.h"
#include "pages/HomePage.h"
#include "widgets/VisualComponents.h"
#include "widgets/VisualTokens.h"

#include <QEvent>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {
constexpr int ControlTimeoutMs = 15'000;
}

MainWindow::MainWindow(QWidget* parent)
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
    m_stack->addWidget(m_companionPage);
    m_stack->addWidget(m_homePage);
    root->addWidget(m_stack);

    m_toast = new ToastWidget(this);
    m_toast->setAccessibleName(QStringLiteral("功能提示"));

    m_controlTimeout = new QTimer(this);
    m_controlTimeout->setObjectName(QStringLiteral("controlTimeout"));
    m_controlTimeout->setSingleShot(true);
    m_controlTimeout->setInterval(ControlTimeoutMs);

    connect(m_companionPage, &CompanionPage::controlRequested,
            this, &MainWindow::showHome);
    connect(m_homePage, &HomePage::talkRequested,
            this, [this] { showUnavailable(QStringLiteral("语音")); });
    connect(m_homePage, &HomePage::careRequested,
            this, [this] { showUnavailable(QStringLiteral("今日关怀")); });
    connect(m_homePage, &HomePage::reminderRequested,
            this, [this] { showUnavailable(QStringLiteral("提醒")); });
    connect(m_homePage, &HomePage::settingsRequested,
            this, [this] { showUnavailable(QStringLiteral("设置")); });
    connect(m_controlTimeout, &QTimer::timeout,
            this, &MainWindow::showCompanion);

    m_homePage->installEventFilter(this);
    for (QObject* child : m_homePage->findChildren<QObject*>())
        child->installEventFilter(this);
    showCompanion();
}

MainWindow::PageId MainWindow::currentPage() const
{
    return m_stack->currentWidget() == m_homePage
        ? PageId::Home : PageId::Companion;
}

void MainWindow::showPage(PageId page)
{
    if (page == PageId::Home)
        showHome();
    else
        showCompanion();
}

void MainWindow::showToast(const QString& text)
{
    m_toast->showMessage(text);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    const auto* watchedWidget = qobject_cast<QWidget*>(watched);
    const bool belongsToHome = watched == m_homePage
        || (watchedWidget && m_homePage->isAncestorOf(watchedWidget));
    if (belongsToHome && currentPage() == PageId::Home) {
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::TouchBegin:
        case QEvent::KeyPress:
            restartControlTimeout();
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void MainWindow::showCompanion()
{
    m_controlTimeout->stop();
    m_toast->hide();
    m_stack->setCurrentWidget(m_companionPage);
}

void MainWindow::showHome()
{
    m_stack->setCurrentWidget(m_homePage);
    restartControlTimeout();
}

void MainWindow::restartControlTimeout()
{
    if (currentPage() == PageId::Home)
        m_controlTimeout->start();
}

void MainWindow::showUnavailable(const QString& capabilityName)
{
    restartControlTimeout();
    showToast(tr("%1功能将在后续版本接入").arg(capabilityName));
}
