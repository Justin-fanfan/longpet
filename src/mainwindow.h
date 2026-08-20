#pragma once

#include "model/ReminderModels.h"
#include "model/SettingsModels.h"
#include "model/SystemModels.h"

#include <QWidget>

class CarePage;
class CompanionPage;
class EmergencyPage;
class HomePage;
class QEvent;
class QStackedWidget;
class ReminderEditPage;
class ReminderAlertPage;
class ReminderPage;
class SettingsPage;
class ToastWidget;

class MainWindow final : public QWidget {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    enum class PageId {
        Companion,
        Home,
        Care,
        Reminder,
        ReminderEdit,
        Settings,
        ReminderAlert,
        Emergency
    };

    PageId currentPage() const;
    void showPage(PageId page);
    void showToast(const QString& text);
    void setReminders(const QList<Reminder>& reminders);
    void setReminderDraft(const ReminderDraft& draft);
    void setReminderPresentation(const ReminderPresentation& presentation);
    void clearReminderPresentation();
    void setCareSummary(const CareSummary& summary);
    void setSettings(const UserSettings& settings);
    void setSystemStatus(const SystemStatus& status);
    void setDeviceSummary(const DeviceSummary& summary);

signals:
    void controlRequested();
    void userActivity(PageId page);
    void talkRequested();
    void careRequested();
    void reminderRequested();
    void settingsRequested();
    void homeRequested();
    void addReminderRequested();
    void editReminderRequested(ReminderId id);
    void completeReminderRequested(ReminderId id);
    void acknowledgeReminderAlertRequested(ReminderEventId eventId);
    void completeReminderAlertRequested(ReminderEventId eventId);
    void saveReminderRequested(const ReminderDraft& draft);
    void deleteReminderRequested(ReminderId id);
    void cancelReminderEditRequested();
    void recordWaterRequested();
    void volumeChangeRequested(int value);
    void brightnessChangeRequested(int value);
    void petStyleChangeRequested(const QString& style);
    void pairFamilyRequested();
    void emergencyDismissRequested();
    void emergencyContactRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QWidget* pageWidget(PageId page) const;
    QStackedWidget* m_stack = nullptr;
    CompanionPage* m_companionPage = nullptr;
    HomePage* m_homePage = nullptr;
    CarePage* m_carePage = nullptr;
    ReminderPage* m_reminderPage = nullptr;
    ReminderEditPage* m_reminderEditPage = nullptr;
    ReminderAlertPage* m_reminderAlertPage = nullptr;
    SettingsPage* m_settingsPage = nullptr;
    EmergencyPage* m_emergencyPage = nullptr;
    ToastWidget* m_toast = nullptr;
    PageId m_currentPage = PageId::Companion;
};
