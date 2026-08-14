#include "CareService.h"

#include "data/CareEventRepository.h"
#include "services/ReminderService.h"

CareService::CareService(CareEventRepository* repository,
                         ReminderService* reminderService,
                         QObject* parent)
    : QObject(parent),
      m_repository(repository),
      m_reminderService(reminderService)
{
    connect(m_reminderService, &ReminderService::remindersChanged,
            this, &CareService::summaryChanged);
}

CareSummary CareService::todaySummary(QString* error) const
{
    CareSummary summary;
    const QDate today = QDate::currentDate();
    summary.waterCompleted = m_repository->sumForDate(QStringLiteral("water"), today, error);
    summary.activityMinutes = m_repository->sumForDate(QStringLiteral("activity_minutes"), today, error);
    summary.interactionCount = m_repository->sumForDate(QStringLiteral("interaction"), today, error);

    const QList<Reminder> reminders = m_reminderService->reminders(error);
    for (const Reminder& reminder : reminders) {
        if (reminder.type != ReminderType::Medicine || !reminder.enabled)
            continue;
        if (reminder.repeatRule == ReminderRepeatRule::Once
            && reminder.scheduledDate != today) {
            continue;
        }
        if (reminder.repeatRule == ReminderRepeatRule::Weekdays
            && (today.dayOfWeek() < Qt::Monday || today.dayOfWeek() > Qt::Friday)) {
            continue;
        }
        ++summary.medicineTotal;
        if (reminder.status == ReminderOccurrenceStatus::Completed)
            ++summary.medicineCompleted;
    }
    summary.lastUpdated = QDateTime::currentDateTime();
    return summary;
}

ServiceResult CareService::recordWater()
{
    QString error;
    const int current = m_repository->sumForDate(QStringLiteral("water"),
                                                  QDate::currentDate(), &error);
    if (!error.isEmpty())
        return {false, error, 0};
    if (current >= 8)
        return {false, QStringLiteral("今天的 8 杯饮水目标已经完成"), 0};
    if (!m_repository->append(QStringLiteral("water"), 1,
                              QStringLiteral("local_ui"), &error)) {
        return {false, error, 0};
    }
    emit summaryChanged();
    return {true, {}, 0};
}

ServiceResult CareService::recordActivityMinutes(int minutes, const QString& source)
{
    if (minutes <= 0 || minutes > 1'440)
        return {false, QStringLiteral("活动时长必须在 1 到 1440 分钟之间"), 0};
    QString error;
    if (!m_repository->append(QStringLiteral("activity_minutes"), minutes,
                              source.simplified().isEmpty() ? QStringLiteral("device")
                                                            : source.simplified(),
                              &error)) {
        return {false, error, 0};
    }
    emit summaryChanged();
    return {true, {}, 0};
}

ServiceResult CareService::recordInteraction(int count, const QString& source)
{
    if (count <= 0 || count > 1'000)
        return {false, QStringLiteral("互动次数必须在 1 到 1000 之间"), 0};
    QString error;
    if (!m_repository->append(QStringLiteral("interaction"), count,
                              source.simplified().isEmpty() ? QStringLiteral("perception")
                                                            : source.simplified(),
                              &error)) {
        return {false, error, 0};
    }
    emit summaryChanged();
    return {true, {}, 0};
}
