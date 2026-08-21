#pragma once

#include "model/SettingsModels.h"

#include <QWidget>

class QLabel;
class QSlider;
class SettingRow;

class SettingsPage final : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(QWidget* parent = nullptr);
    void setSettings(const UserSettings& settings);
    void setDeviceSummary(const DeviceSummary& summary);
    void setDeveloperMode(bool enabled);

signals:
    void backRequested();
    void volumeChangeRequested(int value);
    void brightnessChangeRequested(int value);
    void petStyleChangeRequested(const QString& style);
    void pairFamilyRequested();
    void developerRequested();

private:
    void updateValueLabel(QLabel* label, int value);
    void updateBrightnessPresentation();

    QSlider* m_volumeSlider = nullptr;
    QSlider* m_brightnessSlider = nullptr;
    QLabel* m_volumeValue = nullptr;
    QLabel* m_brightnessValue = nullptr;
    QLabel* m_networkSummary = nullptr;
    QLabel* m_keywordSpottingSummary = nullptr;
    QLabel* m_powerSummary = nullptr;
    SettingRow* m_soundRow = nullptr;
    SettingRow* m_brightnessRow = nullptr;
    SettingRow* m_familyRow = nullptr;
    QLabel* m_versionSummary = nullptr;
    class QPushButton* m_petStyleButton = nullptr;
    class QPushButton* m_developerButton = nullptr;
    UserSettings m_settings;
    bool m_binaryBrightness = false;
    bool m_updating = false;
};
