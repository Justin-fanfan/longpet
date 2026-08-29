#pragma once

#include "model/ReminderModels.h"
#include "model/SettingsModels.h"
#include "model/SystemModels.h"

#include <QDateTime>
#include <QList>
#include <QString>

class CareService;
class ReminderService;
class SettingsService;
class SystemService;

struct FamilyLinkStatusSnapshot {
    QString deviceId;
    QString deviceName;
    QDateTime observedAt;
    DeviceSummary device;
    SystemStatus system;
    CareSummary care;
};

struct FamilyLinkSettingsSnapshot {
    UserSettings settings;
    DeviceSummary device;
    int revision = 0;
};

class FamilyLinkService final {
public:
    FamilyLinkService(ReminderService* reminderService,
                      CareService* careService,
                      SettingsService* settingsService,
                      SystemService* systemService);

    bool status(FamilyLinkStatusSnapshot* snapshot, QString* error = nullptr) const;
    bool settings(FamilyLinkSettingsSnapshot* snapshot, QString* error = nullptr) const;
    QList<Reminder> reminders(QString* error = nullptr) const;

private:
    static QString configuredDeviceId();
    static QString configuredDeviceName();

    ReminderService* m_reminderService = nullptr;
    CareService* m_careService = nullptr;
    SettingsService* m_settingsService = nullptr;
    SystemService* m_systemService = nullptr;
};
