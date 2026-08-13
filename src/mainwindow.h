#pragma once

#include <QWidget>

class CompanionPage;
class HomePage;
class QEvent;
class QStackedWidget;
class QTimer;
class ToastWidget;

class MainWindow final : public QWidget {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    enum class PageId {
        Companion,
        Home
    };

    PageId currentPage() const;
    void showPage(PageId page);
    void showToast(const QString& text);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void showCompanion();
    void showHome();
    void restartControlTimeout();
    void showUnavailable(const QString& capabilityName);

    QStackedWidget* m_stack = nullptr;
    QTimer* m_controlTimeout = nullptr;
    CompanionPage* m_companionPage = nullptr;
    HomePage* m_homePage = nullptr;
    ToastWidget* m_toast = nullptr;
};
