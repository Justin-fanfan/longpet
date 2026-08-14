#include "ReminderRepository.h"

#include <QSqlError>
#include <QSqlQuery>

namespace {
QString typeToString(ReminderType type)
{
    switch (type) {
    case ReminderType::Medicine: return QStringLiteral("medicine");
    case ReminderType::Water: return QStringLiteral("water");
    case ReminderType::Other: return QStringLiteral("other");
    }
    return QStringLiteral("other");
}

ReminderType typeFromString(const QString& type)
{
    if (type == QStringLiteral("medicine")) return ReminderType::Medicine;
    if (type == QStringLiteral("water")) return ReminderType::Water;
    return ReminderType::Other;
}

QString repeatToString(ReminderRepeatRule rule)
{
    switch (rule) {
    case ReminderRepeatRule::Daily: return QStringLiteral("daily");
    case ReminderRepeatRule::Weekdays: return QStringLiteral("weekdays");
    case ReminderRepeatRule::Once: return QStringLiteral("once");
    }
    return QStringLiteral("daily");
}

ReminderRepeatRule repeatFromString(const QString& rule)
{
    if (rule == QStringLiteral("weekdays")) return ReminderRepeatRule::Weekdays;
    if (rule == QStringLiteral("once")) return ReminderRepeatRule::Once;
    return ReminderRepeatRule::Daily;
}

QString statusToString(ReminderOccurrenceStatus status)
{
    switch (status) {
    case ReminderOccurrenceStatus::Completed: return QStringLiteral("completed");
    case ReminderOccurrenceStatus::Missed: return QStringLiteral("missed");
    case ReminderOccurrenceStatus::Disabled: return QStringLiteral("disabled");
    case ReminderOccurrenceStatus::Pending: return QStringLiteral("pending");
    }
    return QStringLiteral("pending");
}

Reminder reminderFromQuery(const QSqlQuery& query)
{
    Reminder reminder;
    reminder.id = query.value(QStringLiteral("id")).toLongLong();
    reminder.type = typeFromString(query.value(QStringLiteral("type")).toString());
    reminder.title = query.value(QStringLiteral("title")).toString();
    reminder.timeOfDay = QTime::fromString(query.value(QStringLiteral("time_of_day")).toString(),
                                           QStringLiteral("HH:mm"));
    reminder.scheduledDate = QDate::fromString(query.value(QStringLiteral("scheduled_date")).toString(),
                                               Qt::ISODate);
    reminder.repeatRule = repeatFromString(query.value(QStringLiteral("repeat_rule")).toString());
    reminder.enabled = query.value(QStringLiteral("enabled")).toBool();
    reminder.revision = query.value(QStringLiteral("revision")).toInt();
    reminder.createdAt = QDateTime::fromString(query.value(QStringLiteral("created_at")).toString(), Qt::ISODate);
    reminder.updatedAt = QDateTime::fromString(query.value(QStringLiteral("updated_at")).toString(), Qt::ISODate);
    return reminder;
}

ServiceResult failedResult(const QSqlQuery& query)
{
    return {false, query.lastError().text(), 0};
}
}

ReminderRepository::ReminderRepository(const QSqlDatabase& database)
    : m_database(database)
{
}

QList<Reminder> ReminderRepository::all(QString* error) const
{
    QList<Reminder> reminders;
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("SELECT * FROM reminders ORDER BY time_of_day, id"))) {
        if (error)
            *error = query.lastError().text();
        return reminders;
    }
    while (query.next())
        reminders.append(reminderFromQuery(query));
    return reminders;
}

Reminder ReminderRepository::find(ReminderId id, bool* found, QString* error) const
{
    if (found)
        *found = false;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT * FROM reminders WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        if (error)
            *error = query.lastError().text();
        return {};
    }
    if (!query.next())
        return {};
    if (found)
        *found = true;
    return reminderFromQuery(query);
}

ServiceResult ReminderRepository::insert(const ReminderDraft& draft)
{
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO reminders(type,title,time_of_day,scheduled_date,repeat_rule,enabled,revision,created_at,updated_at) "
        "VALUES(?,?,?,?,?,?,1,?,?)"));
    query.addBindValue(typeToString(draft.type));
    query.addBindValue(draft.title);
    query.addBindValue(draft.timeOfDay.toString(QStringLiteral("HH:mm")));
    query.addBindValue(draft.scheduledDate.toString(Qt::ISODate));
    query.addBindValue(repeatToString(draft.repeatRule));
    query.addBindValue(draft.enabled);
    query.addBindValue(now);
    query.addBindValue(now);
    if (!query.exec())
        return failedResult(query);
    return {true, {}, query.lastInsertId().toLongLong()};
}

ServiceResult ReminderRepository::update(ReminderId id, const ReminderDraft& draft)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE reminders SET type=?,title=?,time_of_day=?,scheduled_date=?,repeat_rule=?,enabled=?,"
        "revision=revision+1,updated_at=? WHERE id=? AND revision=?"));
    query.addBindValue(typeToString(draft.type));
    query.addBindValue(draft.title);
    query.addBindValue(draft.timeOfDay.toString(QStringLiteral("HH:mm")));
    query.addBindValue(draft.scheduledDate.toString(Qt::ISODate));
    query.addBindValue(repeatToString(draft.repeatRule));
    query.addBindValue(draft.enabled);
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    query.addBindValue(id);
    query.addBindValue(draft.expectedRevision);
    if (!query.exec())
        return failedResult(query);
    if (query.numRowsAffected() != 1)
        return {false, QStringLiteral("提醒已被其他操作修改，请刷新后重试"), 0};
    return {true, {}, id};
}

ServiceResult ReminderRepository::remove(ReminderId id)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM reminders WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec())
        return failedResult(query);
    if (query.numRowsAffected() != 1)
        return {false, QStringLiteral("提醒不存在"), 0};
    return {true, {}, id};
}

bool ReminderRepository::addEvent(ReminderId id, const QDateTime& scheduledAt,
                                  ReminderOccurrenceStatus status, QString* error)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO reminder_events(reminder_id,scheduled_at,status,completed_at) VALUES(?,?,?,?)"));
    query.addBindValue(id);
    query.addBindValue(scheduledAt.toUTC().toString(Qt::ISODate));
    query.addBindValue(statusToString(status));
    query.addBindValue(status == ReminderOccurrenceStatus::Completed
        ? QDateTime::currentDateTimeUtc().toString(Qt::ISODate) : QVariant());
    if (query.exec())
        return true;
    if (error)
        *error = query.lastError().text();
    return false;
}

bool ReminderRepository::hasEventForDate(ReminderId id, const QDate& date,
                                         QString* error) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT 1 FROM reminder_events WHERE reminder_id=? AND scheduled_at>=? "
        "AND scheduled_at<? LIMIT 1"));
    query.addBindValue(id);
    query.addBindValue(QDateTime(date.startOfDay()).toUTC().toString(Qt::ISODate));
    query.addBindValue(QDateTime(date.addDays(1).startOfDay()).toUTC().toString(Qt::ISODate));
    if (!query.exec()) {
        if (error)
            *error = query.lastError().text();
        return false;
    }
    return query.next();
}

ReminderOccurrenceStatus ReminderRepository::statusForDate(ReminderId id, const QDate& date,
                                                           QString* error) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT status FROM reminder_events WHERE reminder_id=? AND scheduled_at>=? AND scheduled_at<? "
        "ORDER BY id DESC LIMIT 1"));
    query.addBindValue(id);
    query.addBindValue(QDateTime(date.startOfDay()).toUTC().toString(Qt::ISODate));
    query.addBindValue(QDateTime(date.addDays(1).startOfDay()).toUTC().toString(Qt::ISODate));
    if (!query.exec()) {
        if (error)
            *error = query.lastError().text();
        return ReminderOccurrenceStatus::Pending;
    }
    if (!query.next())
        return ReminderOccurrenceStatus::Pending;
    const QString status = query.value(0).toString();
    if (status == QStringLiteral("completed")) return ReminderOccurrenceStatus::Completed;
    if (status == QStringLiteral("missed")) return ReminderOccurrenceStatus::Missed;
    return ReminderOccurrenceStatus::Pending;
}
