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
    snapshot->revision = m_settingsService->revision(&settingsError);
    if (!settingsError.isEmpty()) {
        if (error)
            *error = settingsError;
        return false;
    }
    return true;
}

SettingsUpdateResult FamilyLinkService::updateSettings(
    const SettingsUpdateRequest& request) const
{
    if (!m_settingsService || !m_systemService) {
        SettingsUpdateResult result;
        result.error = QStringLiteral("FamilyLink 设置服务未初始化");
        result.code = SettingsUpdateErrorCode::Storage;
        return result;
    }
    const DeviceSummary device = m_systemService->deviceSummary();
    if (request.volume.has_value() && !device.audioControlAvailable) {
        SettingsUpdateResult result;
        result.error = QStringLiteral("当前音量控制不可用：%1").arg(device.audioSummary);
        result.code = SettingsUpdateErrorCode::CapabilityUnavailable;
        return result;
    }
    if (request.brightness.has_value() && !device.brightnessControlAvailable) {
        SettingsUpdateResult result;
        result.error = QStringLiteral("当前亮度控制不可用：%1")
                           .arg(device.brightnessSummary);
        result.code = SettingsUpdateErrorCode::CapabilityUnavailable;
        return result;
    }
    return m_settingsService->updateSettings(request);
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

ServiceResult FamilyLinkService::saveReminder(const ReminderDraft& draft,
                                              Reminder* saved) const
{
    if (!m_reminderService) {
        return {false, QStringLiteral("FamilyLink 提醒服务未初始化"), 0,
                ServiceErrorCode::Storage};
    }
    const ServiceResult result = m_reminderService->save(draft);
    if (!result.success)
        return result;

    bool found = false;
    QString error;
    const Reminder current = m_reminderService->reminder(result.id, &found, &error);
    if (!error.isEmpty())
        return {false, error, result.id, ServiceErrorCode::Storage};
    if (!found)
        return {false, QStringLiteral("保存后的提醒不存在"), result.id,
                ServiceErrorCode::Storage};
    if (saved)
        *saved = current;
    return result;
}

ServiceResult FamilyLinkService::removeReminder(ReminderId id,
                                                int expectedRevision) const
{
    if (!m_reminderService) {
        return {false, QStringLiteral("FamilyLink 提醒服务未初始化"), 0,
                ServiceErrorCode::Storage};
    }
    return m_reminderService->remove(id, expectedRevision);
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
