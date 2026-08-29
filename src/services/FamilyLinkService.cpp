#include "FamilyLinkService.h"

#include "services/CareService.h"
#include "services/ReminderService.h"
#include "services/SettingsService.h"
#include "services/SystemService.h"

#include <QSysInfo>

namespace {
QString normalizedHostName()
{
    const QString host = QSysInfo::machineHostName().simplified().toLower();
    QString normalized;
    normalized.reserve(host.size());
    bool previousDash = false;
    for (const QChar character : host) {
        const bool accepted = character.isLetterOrNumber();
        if (accepted) {
            normalized.append(character);
            previousDash = false;
        } else if (!previousDash && !normalized.isEmpty()) {
            normalized.append(QLatin1Char('-'));
            previousDash = true;
        }
    }
    while (normalized.endsWith(QLatin1Char('-')))
        normalized.chop(1);
    return normalized.isEmpty() ? QStringLiteral("device") : normalized;
}
}

FamilyLinkService::FamilyLinkService(ReminderService* reminderService,
                                     CareService* careService,
                                     SettingsService* settingsService,
                                     SystemService* systemService)
    : m_reminderService(reminderService),
      m_careService(careService),
      m_settingsService(settingsService),
      m_systemService(systemService)
{
}

bool FamilyLinkService::status(FamilyLinkStatusSnapshot* snapshot, QString* error) const
{
    if (!snapshot || !m_careService || !m_systemService) {
        if (error)
            *error = QStringLiteral("FamilyLink 状态服务未初始化");
        return false;
    }

    QString careError;
    const CareSummary care = m_careService->todaySummary(&careError);
    if (!careError.isEmpty()) {
        if (error)
            *error = careError;
        return false;
    }

    snapshot->deviceId = configuredDeviceId();
    snapshot->deviceName = configuredDeviceName();
    snapshot->observedAt = QDateTime::currentDateTime();
    snapshot->device = m_systemService->deviceSummary();
    snapshot->system = m_systemService->status();
    snapshot->system.currentDateTime = snapshot->observedAt;
    snapshot->care = care;
    return true;
}

bool FamilyLinkService::settings(FamilyLinkSettingsSnapshot* snapshot, QString* error) const
{
    if (!snapshot || !m_settingsService || !m_systemService) {
        if (error)
            *error = QStringLiteral("FamilyLink 设置服务未初始化");
        return false;
    }

    QString settingsError;
    const UserSettings userSettings = m_settingsService->settings(&settingsError);
    if (!settingsError.isEmpty()) {
        if (error)
            *error = settingsError;
        return false;
    }

    snapshot->settings = userSettings;
    snapshot->device = m_systemService->deviceSummary();
    snapshot->revision = 0;
    return true;
}

QList<Reminder> FamilyLinkService::reminders(QString* error) const
{
    if (!m_reminderService) {
        if (error)
            *error = QStringLiteral("FamilyLink 提醒服务未初始化");
        return {};
    }
    return m_reminderService->reminders(error);
}

QString FamilyLinkService::configuredDeviceId()
{
    const QString configured = qEnvironmentVariable("LONGPET_DEVICE_ID").simplified();
    return configured.isEmpty()
        ? QStringLiteral("longpet-%1").arg(normalizedHostName())
        : configured;
}

QString FamilyLinkService::configuredDeviceName()
{
    const QString configured = qEnvironmentVariable("LONGPET_DEVICE_NAME").simplified();
    return configured.isEmpty()
        ? QStringLiteral("LongPet %1").arg(QSysInfo::machineHostName().simplified())
        : configured;
}
