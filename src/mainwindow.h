#pragma once

#include "model/ReminderModels.h"
#include "model/NetworkModels.h"
#include "model/SettingsModels.h"
#include "model/SystemModels.h"
#include "model/VideoCallModels.h"

#include <QWidget>

class QImage;

class CarePage;
class CompanionPage;
class HomePage;
class NetworkSetupPage;
class QEvent;
class QStackedWidget;
class ReminderEditPage;
class ReminderPage;
class SettingsPage;
class ToastWidget;
class VideoCallPage;

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
        VideoCall,
        Settings,
        NetworkSetup
    };

    PageId currentPage() const;
    void showPage(PageId page);
    void showToast(const QString& text);
    void setReminders(const QList<Reminder>& reminders);
    void setReminderDraft(const ReminderDraft& draft);
    void setCareSummary(const CareSummary& summary);
    void setSettings(const UserSettings& settings);
    void setSystemStatus(const SystemStatus& status);
    void setDeviceSummary(const DeviceSummary& summary);
    void setVideoCallSnapshot(const VideoCallSnapshot& snapshot);
    void setRemoteVideoFrame(const QImage& frame);
    void setWifiScanStarted();
    void setWifiNetworks(const QList<WifiNetwork>& networks);
    void setWifiScanFailed(const QString& error);
    void setWifiConnectionStarted(const QString& ssid);
    void setWifiConnectionSucceeded(const QString& ssid);
    void setWifiConnectionFailed(const QString& ssid, const QString& error);

signals:
    void controlRequested();
    void userActivity(PageId page);
    void talkRequested();
    void careRequested();
    void reminderRequested();
    void videoCallRequested();
    void videoCallHangUpRequested();
    void settingsRequested();
    void homeRequested();
    void addReminderRequested();
    void editReminderRequested(ReminderId id);
    void completeReminderRequested(ReminderId id);
    void saveReminderRequested(const ReminderDraft& draft);
    void deleteReminderRequested(ReminderId id);
    void cancelReminderEditRequested();
    void recordWaterRequested();
    void volumeChangeRequested(int value);
    void brightnessChangeRequested(int value);
    void networkSetupRequested();
    void wifiScanRequested();
    void wifiConnectRequested(const WifiNetwork& network, const QString& password);
    void petStyleChangeRequested(const QString& style);
    void pairFamilyRequested();

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
    VideoCallPage* m_videoCallPage = nullptr;
    SettingsPage* m_settingsPage = nullptr;
    NetworkSetupPage* m_networkSetupPage = nullptr;
    ToastWidget* m_toast = nullptr;
    PageId m_currentPage = PageId::Companion;
};
