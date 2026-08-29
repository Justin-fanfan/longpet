#include "ReminderService.h"

#include "data/ReminderRepository.h"

namespace {
constexpr int SchedulerRecheckMs = 60'000;

QString defaultTitle(ReminderType type)
{
    switch (type) {
    case ReminderType::Medicine: return QStringLiteral("按时用药");
    case ReminderType::Water: return QStringLiteral("记得喝水");
    case ReminderType::Other: return QStringLiteral("日常提醒");
    }
    return QStringLiteral("日常提醒");
}
}

ReminderService::ReminderService(ReminderRepository* repository, QObject* parent)
    : QObject(parent), m_repository(repository)
{
    m_scheduler.setObjectName(QStringLiteral("reminderScheduler"));
    m_scheduler.setSingleShot(true);
    m_scheduler.setTimerType(Qt::CoarseTimer);
    connect(&m_scheduler, &QTimer::timeout,
            this, &ReminderService::checkDueReminders);
}

QList<Reminder> ReminderService::reminders(QString* error) const
{
    QList<Reminder> result = m_repository->all(error);
    const QDateTime now = QDateTime::currentDateTime();
    for (Reminder& reminder : result) {
        if (!reminder.enabled) {
            reminder.status = ReminderOccurrenceStatus::Disabled;
            continue;
        }
        reminder.status = m_repository->statusForDate(reminder.id, now.date(), error);
        const bool occurredWithoutDelivery = reminder.status == ReminderOccurrenceStatus::Pending
            && !m_repository->hasEventForDate(reminder.id, now.date(), error)
            && ((appliesOnDate(reminder, now.date()) && reminder.timeOfDay < now.time())
                || (reminder.repeatRule == ReminderRepeatRule::Once
                    && reminder.scheduledDate < now.date()));
        if (occurredWithoutDelivery)
            reminder.status = ReminderOccurrenceStatus::Missed;
    }
    return result;
}

Reminder ReminderService::reminder(ReminderId id, bool* found, QString* error) const
{
    return m_repository->find(id, found, error);
}

ServiceResult ReminderService::save(const ReminderDraft& draft)
{
    ReminderDraft validated = draft;
    validated.title = validated.title.simplified();
    if (validated.title.isEmpty())
        validated.title = defaultTitle(validated.type);
    if (validated.title.size() > 40)
        return {false, QStringLiteral("提醒内容不能超过 40 个字"), 0,
                ServiceErrorCode::Validation};
    if (!validated.timeOfDay.isValid())
        return {false, QStringLiteral("请选择有效的提醒时间"), 0,
                ServiceErrorCode::Validation};
    if (!validated.scheduledDate.isValid())
        validated.scheduledDate = QDate::currentDate();
    if (validated.repeatRule == ReminderRepeatRule::Once
        && QDateTime(validated.scheduledDate, validated.timeOfDay)
               <= QDateTime::currentDateTime()) {
        return {false, QStringLiteral("单次提醒时间必须晚于当前时间"), 0,
                ServiceErrorCode::Validation};
    }

    const ServiceResult result = validated.id == 0
        ? m_repository->insert(validated)
        : m_repository->update(validated.id, validated);
    if (!result.success)
        return result;
    emit remindersChanged();
    scheduleNext();
    return result;
}

ServiceResult ReminderService::remove(ReminderId id, int expectedRevision)
{
    const ServiceResult result = m_repository->remove(id, expectedRevision);
    if (result.success) {
        emit remindersChanged();
        scheduleNext();
    }
    return result;
}

ServiceResult ReminderService::markCompleted(ReminderId id)
{
    bool found = false;
    QString error;
    const Reminder current = m_repository->find(id, &found, &error);
    if (!error.isEmpty())
        return {false, error, 0};
    if (!found)
        return {false, QStringLiteral("提醒不存在"), 0};
    if (!current.enabled)
        return {false, QStringLiteral("已停用的提醒不能标记完成"), 0};
    const QDate today = QDate::currentDate();
    if (!appliesOnDate(current, today))
        return {false, QStringLiteral("这条提醒今天不需要完成"), 0};
    if (m_repository->statusForDate(id, today, &error)
        == ReminderOccurrenceStatus::Completed) {
        return {false, QStringLiteral("这条提醒今天已经完成"), 0};
    }
    if (!error.isEmpty())
        return {false, error, 0};
    if (!m_repository->addEvent(id, QDateTime(today, current.timeOfDay),
                                ReminderOccurrenceStatus::Completed, &error)) {
        return {false, error, 0};
    }
    emit remindersChanged();
    return {true, {}, id};
}

void ReminderService::start()
{
    // Check the current minute first so a process started shortly after the
    // scheduled time still delivers the occurrence exactly once.
    checkDueReminders();
}

void ReminderService::stop()
{
    m_scheduler.stop();
}

bool ReminderService::appliesOnDate(const Reminder& reminder, const QDate& date) const
{
    if (reminder.repeatRule == ReminderRepeatRule::Once)
        return date == reminder.scheduledDate;
    if (reminder.repeatRule == ReminderRepeatRule::Weekdays)
        return date.dayOfWeek() >= Qt::Monday && date.dayOfWeek() <= Qt::Friday;
    return true;
}

QDateTime ReminderService::nextOccurrence(const Reminder& reminder, const QDateTime& after) const
{
    if (!reminder.enabled)
        return {};
    if (reminder.repeatRule == ReminderRepeatRule::Once) {
        const QDateTime candidate(reminder.scheduledDate, reminder.timeOfDay);
        return candidate > after ? candidate : QDateTime();
    }
    for (int offset = 0; offset <= 7; ++offset) {
        const QDate date = after.date().addDays(offset);
        if (!appliesOnDate(reminder, date))
            continue;
        const QDateTime candidate(date, reminder.timeOfDay);
        if (candidate > after)
            return candidate;
    }
    return {};
}

void ReminderService::scheduleNext()
{
    m_scheduler.stop();
    QString error;
    const QList<Reminder> all = m_repository->all(&error);
    if (!error.isEmpty()) {
        emit errorOccurred(error);
        return;
    }
    const QDateTime now = QDateTime::currentDateTime();
    QDateTime nearest;
    for (const Reminder& reminder : all) {
        const QDateTime occurrence = nextOccurrence(reminder, now);
        if (occurrence.isValid() && (!nearest.isValid() || occurrence < nearest))
            nearest = occurrence;
    }
    if (!nearest.isValid())
        return;
    const qint64 remaining = qMax<qint64>(1, now.msecsTo(nearest));
    m_scheduler.start(static_cast<int>(qMin<qint64>(remaining, SchedulerRecheckMs)));
}

void ReminderService::checkDueReminders()
{
    QString error;
    const QList<Reminder> all = m_repository->all(&error);
    if (!error.isEmpty()) {
        emit errorOccurred(error);
        scheduleNext();
        return;
    }

    const QDateTime now = QDateTime::currentDateTime();
    for (const Reminder& reminder : all) {
        if (!reminder.enabled || !appliesOnDate(reminder, now.date()))
            continue;
        const qint64 elapsed = QDateTime(now.date(), reminder.timeOfDay).secsTo(now);
        if (elapsed < 0 || elapsed > 60)
            continue;
        const bool alreadyDelivered = m_repository->hasEventForDate(
            reminder.id, now.date(), &error);
        if (!error.isEmpty()) {
            emit errorOccurred(error);
            error.clear();
            continue;
        }
        if (alreadyDelivered) {
            continue;
        }
        if (!m_repository->addEvent(reminder.id, QDateTime(now.date(), reminder.timeOfDay),
                                    ReminderOccurrenceStatus::Pending, &error)) {
            emit errorOccurred(error);
            continue;
        }
        emit reminderTriggered(reminder);
    }
    emit remindersChanged();
    scheduleNext();
}
