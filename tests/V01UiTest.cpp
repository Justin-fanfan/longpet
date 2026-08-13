#include "mainwindow.h"
#include "pages/CarePage.h"
#include "pages/CompanionPage.h"
#include "pages/ConversationPage.h"
#include "pages/EmergencyPage.h"
#include "pages/HomePage.h"
#include "pages/ReminderEditPage.h"
#include "pages/ReminderPage.h"
#include "pages/SettingsPage.h"
#include "pages/SleepPage.h"
#include "widgets/PetFaceWidget.h"
#include "widgets/VisualComponents.h"
#include "widgets/VisualTokens.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QSignalSpy>
#include <QSvgRenderer>
#include <QTest>
#include <QTimer>

#include <memory>
#include <vector>

class V01UiTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void productionResourcesAreEmbedded();
    void futureProductPagesConstructAndRender();
    void companionStartsInLowCostExpression();
    void pagesExposeSemanticSignals();
    void statusBarContainsTheFullSettingsButton();
    void touchRevealsHomeAndTimeoutReturnsToCompanion();
    void interactionInsideHomeRestartsTimeout();
    void unavailableCapabilitiesUseTruthfulToast();
    void renderReferenceCaptures();
};

void V01UiTest::initTestCase()
{
    QFile styleFile(QStringLiteral(":/styles/app.qss"));
    QVERIFY2(styleFile.open(QIODevice::ReadOnly | QIODevice::Text),
             "embedded QSS must be readable");
    qApp->setStyleSheet(QString::fromUtf8(styleFile.readAll()));
}

void V01UiTest::productionResourcesAreEmbedded()
{
    const QStringList resources {
        QStringLiteral(":/styles/app.qss"),
        QStringLiteral(":/icons/back.svg"),
        QStringLiteral(":/icons/microphone-dark.svg"),
        QStringLiteral(":/icons/care.svg"),
        QStringLiteral(":/icons/reminder.svg"),
        QStringLiteral(":/icons/settings.svg"),
        QStringLiteral(":/icons/weather-sunny.svg"),
        QStringLiteral(":/icons/wifi.svg"),
        QStringLiteral(":/icons/battery.svg"),
        QStringLiteral(":/icons/pill.svg"),
        QStringLiteral(":/icons/water.svg"),
        QStringLiteral(":/icons/activity.svg"),
        QStringLiteral(":/icons/check.svg"),
        QStringLiteral(":/icons/check-dark.svg"),
        QStringLiteral(":/icons/clock.svg"),
        QStringLiteral(":/icons/alert.svg"),
        QStringLiteral(":/icons/phone-dark.svg"),
        QStringLiteral(":/icons/volume.svg"),
        QStringLiteral(":/icons/brightness.svg"),
        QStringLiteral(":/icons/network.svg"),
        QStringLiteral(":/icons/family.svg"),
        QStringLiteral(":/icons/pet.svg"),
        QStringLiteral(":/icons/info.svg"),
        QStringLiteral(":/icons/home.svg"),
        QStringLiteral(":/icons/plus-dark.svg"),
        QStringLiteral(":/icons/chevron.svg"),
        QStringLiteral(":/icons/palette.svg")
    };

    for (const QString& path : resources)
        QVERIFY2(QFile::exists(path), qPrintable(path));

    for (const QString& path : resources.mid(1)) {
        QSvgRenderer renderer(path);
        QVERIFY2(renderer.isValid(), qPrintable(path));
    }
}

void V01UiTest::futureProductPagesConstructAndRender()
{
    struct NamedPage {
        QString name;
        std::unique_ptr<QWidget> widget;
    };
    std::vector<NamedPage> pages;
    pages.push_back({QStringLiteral("conversation-listening"),
                     std::make_unique<ConversationPage>(ConversationMode::Listening)});
    pages.push_back({QStringLiteral("conversation-thinking"),
                     std::make_unique<ConversationPage>(ConversationMode::Thinking)});
    pages.push_back({QStringLiteral("conversation-speaking"),
                     std::make_unique<ConversationPage>(ConversationMode::Speaking)});
    pages.push_back({QStringLiteral("care"), std::make_unique<CarePage>()});
    pages.push_back({QStringLiteral("reminder"), std::make_unique<ReminderPage>()});
    pages.push_back({QStringLiteral("reminder-edit"), std::make_unique<ReminderEditPage>()});
    pages.push_back({QStringLiteral("settings"), std::make_unique<SettingsPage>()});
    pages.push_back({QStringLiteral("emergency"), std::make_unique<EmergencyPage>()});
    pages.push_back({QStringLiteral("sleep"), std::make_unique<SleepPage>()});

    const QString capturePath = qEnvironmentVariable("LONGPET_TEST_CAPTURE_DIR");
    QDir directory;
    if (!capturePath.isEmpty())
        QVERIFY(directory.mkpath(capturePath));

    for (const auto& page : pages) {
        page.widget->setFixedSize(1024, 600);
        page.widget->show();
        QTest::qWait(5);
        const QPixmap rendered = page.widget->grab();
        QCOMPARE(page.widget->size(), QSize(1024, 600));
        QCOMPARE(rendered.deviceIndependentSize(), QSizeF(1024, 600));
        QVERIFY(!rendered.isNull());
        if (!capturePath.isEmpty()) {
            QVERIFY(rendered.save(QDir(capturePath).filePath(page.name + QStringLiteral(".png"))));
        }
        page.widget->hide();
    }
}

void V01UiTest::companionStartsInLowCostExpression()
{
    MainWindow window;
    window.setFixedSize(LongPetUi::Metrics::CanvasWidth,
                        LongPetUi::Metrics::CanvasHeight);
    window.show();
    QTest::qWait(30);

    QCOMPARE(window.size(), QSize(1024, 600));
    QCOMPARE(window.currentPage(), MainWindow::PageId::Companion);

    const auto* face = window.findChild<PetFaceWidget*>();
    QVERIFY(face);
    QCOMPARE(face->expression(), PetExpression::DefaultOpen);

    const auto* timeout = window.findChild<QTimer*>(QStringLiteral("controlTimeout"));
    QVERIFY(timeout);
    QVERIFY(!timeout->isActive());
}

void V01UiTest::pagesExposeSemanticSignals()
{
    CompanionPage companion;
    companion.resize(1024, 600);
    companion.show();
    QSignalSpy revealSpy(&companion, &CompanionPage::controlRequested);
    auto* reveal = companion.findChild<QPushButton*>(QStringLiteral("companionRevealButton"));
    QVERIFY(reveal);
    QTest::mouseClick(reveal, Qt::LeftButton);
    QCOMPARE(revealSpy.count(), 1);

    HomePage home;
    home.resize(1024, 600);
    home.show();
    QSignalSpy talkSpy(&home, &HomePage::talkRequested);
    QSignalSpy careSpy(&home, &HomePage::careRequested);
    QSignalSpy reminderSpy(&home, &HomePage::reminderRequested);
    QSignalSpy settingsSpy(&home, &HomePage::settingsRequested);

    QTest::mouseClick(home.findChild<QPushButton*>(QStringLiteral("talkButton")),
                      Qt::LeftButton);
    QTest::mouseClick(home.findChild<QPushButton*>(QStringLiteral("careButton")),
                      Qt::LeftButton);
    QTest::mouseClick(home.findChild<QPushButton*>(QStringLiteral("reminderButton")),
                      Qt::LeftButton);
    QTest::mouseClick(home.findChild<QPushButton*>(QStringLiteral("settingsButton")),
                      Qt::LeftButton);

    QCOMPARE(talkSpy.count(), 1);
    QCOMPARE(careSpy.count(), 1);
    QCOMPARE(reminderSpy.count(), 1);
    QCOMPARE(settingsSpy.count(), 1);
}

void V01UiTest::statusBarContainsTheFullSettingsButton()
{
    StatusBarWidget status(true);
    status.setFixedWidth(LongPetUi::Metrics::CanvasWidth);
    status.show();
    QTest::qWait(20);

    auto* settings = status.settingsButton();
    QVERIFY(settings);
    QCOMPARE(status.height(), LongPetUi::Metrics::StatusBarHeight);
    QCOMPARE(settings->size(), QSize(LongPetUi::Metrics::StatusBarHeight,
                                     LongPetUi::Metrics::StatusBarHeight));

    const QRect buttonInStatus(settings->mapTo(&status, QPoint(0, 0)),
                               settings->size());
    QVERIFY2(status.rect().contains(buttonInStatus),
             "the status bar must not clip the settings button");
    QVERIFY2(settings->parentWidget()->rect().contains(settings->geometry()),
             "the settings-button host must contain its full geometry");
}

void V01UiTest::touchRevealsHomeAndTimeoutReturnsToCompanion()
{
    MainWindow window;
    window.setFixedSize(1024, 600);
    window.show();

    auto* timeout = window.findChild<QTimer*>(QStringLiteral("controlTimeout"));
    auto* reveal = window.findChild<QPushButton*>(QStringLiteral("companionRevealButton"));
    QVERIFY(timeout);
    QVERIFY(reveal);
    timeout->setInterval(80);

    QTest::mouseClick(reveal, Qt::LeftButton);
    QCOMPARE(window.currentPage(), MainWindow::PageId::Home);
    QVERIFY(timeout->isActive());
    QTRY_COMPARE_WITH_TIMEOUT(window.currentPage(), MainWindow::PageId::Companion, 500);
    QVERIFY(!timeout->isActive());
}

void V01UiTest::interactionInsideHomeRestartsTimeout()
{
    MainWindow window;
    window.setFixedSize(1024, 600);
    window.show();

    auto* timeout = window.findChild<QTimer*>(QStringLiteral("controlTimeout"));
    auto* face = window.findChild<PetFaceWidget*>(QStringLiteral("homePetFace"));
    QVERIFY(timeout);
    QVERIFY(face);
    timeout->setInterval(180);
    window.showPage(MainWindow::PageId::Home);

    QTest::qWait(110);
    QTest::mouseClick(face, Qt::LeftButton);
    QTest::qWait(110);
    QCOMPARE(window.currentPage(), MainWindow::PageId::Home);
    QTRY_COMPARE_WITH_TIMEOUT(window.currentPage(), MainWindow::PageId::Companion, 300);
}

void V01UiTest::unavailableCapabilitiesUseTruthfulToast()
{
    MainWindow window;
    window.setFixedSize(1024, 600);
    window.show();
    window.showPage(MainWindow::PageId::Home);

    auto* toastLabel = window.findChild<QLabel*>(QStringLiteral("toastWidget"));
    QVERIFY(toastLabel);
    auto* toast = static_cast<ToastWidget*>(toastLabel);

    struct Expectation {
        const char* objectName;
        const char* text;
    };
    const Expectation expectations[] {
        {"talkButton", "语音功能将在后续版本接入"},
        {"careButton", "今日关怀功能将在后续版本接入"},
        {"reminderButton", "提醒功能将在后续版本接入"},
        {"settingsButton", "设置功能将在后续版本接入"}
    };

    for (const auto& expectation : expectations) {
        auto* button = window.findChild<QPushButton*>(
            QString::fromLatin1(expectation.objectName));
        QVERIFY(button);
        QTest::mouseClick(button, Qt::LeftButton);
        QVERIFY(toast->isVisible());
        QCOMPARE(toast->text(), QString::fromUtf8(expectation.text));
    }
}

void V01UiTest::renderReferenceCaptures()
{
    const QString capturePath = qEnvironmentVariable("LONGPET_TEST_CAPTURE_DIR");
    if (capturePath.isEmpty())
        QSKIP("LONGPET_TEST_CAPTURE_DIR is not set");

    QDir directory;
    QVERIFY(directory.mkpath(capturePath));

    MainWindow window;
    window.setFixedSize(1024, 600);
    window.show();
    QTest::qWait(50);
    QVERIFY(window.grab().save(QDir(capturePath).filePath(QStringLiteral("companion.png"))));

    window.showPage(MainWindow::PageId::Home);
    QTest::qWait(50);
    const QPixmap homeCapture = window.grab();
    QCOMPARE(homeCapture.toImage().pixelColor(10, 10),
             LongPetUi::Colors::BackgroundPrimary);
    QCOMPARE(homeCapture.toImage().pixelColor(10, 450),
             LongPetUi::Colors::BackgroundPrimary);
    QVERIFY(homeCapture.save(QDir(capturePath).filePath(QStringLiteral("home.png"))));

    QTest::mouseClick(window.findChild<QPushButton*>(QStringLiteral("talkButton")),
                      Qt::LeftButton);
    QTest::qWait(20);
    QVERIFY(window.grab().save(QDir(capturePath).filePath(QStringLiteral("home-toast.png"))));
}

QTEST_MAIN(V01UiTest)

#include "V01UiTest.moc"
