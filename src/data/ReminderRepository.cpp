#include "ReminderRepository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

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

QString voiceTypeToString(ReminderVoiceType type)
{
    switch (type) {
    case ReminderVoiceType::None: return QStringLiteral("none");
    case ReminderVoiceType::Tts: return QStringLiteral("tts");
    case ReminderVoiceType::AudioAsset: return QStringLiteral("audio_asset");
    }
    return QStringLiteral("none");
}

ReminderVoiceType voiceTypeFromString(const QString& type)
{
    if (type == QStringLiteral("tts")) return ReminderVoiceType::Tts;
    if (type == QStringLiteral("audio_asset")) return ReminderVoiceType::AudioAsset;
    return ReminderVoiceType::None;
}

QString statusToString(ReminderOccurrenceStatus status)
{
    switch (status) {
    case ReminderOccurrenceStatus::Pending: return QStringLiteral("pending");
    case ReminderOccurrenceStatus::Presented: return QStringLiteral("presented");
    case ReminderOccurrenceStatus::Acknowledged: return QStringLiteral("acknowledged");
    case ReminderOccurrenceStatus::Completed: return QStringLiteral("completed");
    case ReminderOccurrenceStatus::Missed: return QStringLiteral("missed");
    case ReminderOccurrenceStatus::Disabled: return QStringLiteral("disabled");
    }
    return QStringLiteral("pending");
}

ReminderOccurrenceStatus statusFromString(const QString& status)
{
    if (status == QStringLiteral("presented")) return ReminderOccurrenceStatus::Presented;
    if (status == QStringLiteral("acknowledged")) return ReminderOccurrenceStatus::Acknowledged;
    if (status == QStringLiteral("completed")) return ReminderOccurrenceStatus::Completed;
    if (status == QStringLiteral("missed")) return ReminderOccurrenceStatus::Missed;
    if (status == QStringLiteral("disabled")) return ReminderOccurrenceStatus::Disabled;
    return ReminderOccurrenceStatus::Pending;
}

QString ackSourceToString(ReminderAckSource source)
{
    switch (source) {
    case ReminderAckSource::None: return QStringLiteral("none");
    case ReminderAckSource::Touch: return QStringLiteral("touch");
    case ReminderAckSource::Voice: return QStringLiteral("voice");
    case ReminderAckSource::Family: return QStringLiteral("family");
    }
    return QStringLiteral("none");
}

ReminderAckSource ackSourceFromString(const QString& source)
{
    if (source == QStringLiteral("touch")) return ReminderAckSource::Touch;
    if (source == QStringLiteral("voice")) return ReminderAckSource::Voice;
    if (source == QStringLiteral("family")) return ReminderAckSource::Family;
    return ReminderAckSource::None;
}

QDateTime dateTimeFromDatabase(const QVariant& value)
{
    return value.isNull() ? QDateTime()
                          : QDateTime::fromString(value.toString(), Qt::ISODate);
}

QString dateTimeToDatabase(const QDateTime& value)
{
    return value.toUTC().toString(Qt::ISODateWithMs);
}

Reminder reminderFromQuery(const QSqlQuery& query)
{
    Reminder reminder;
    reminder.id = query.value(QStringLiteral("id")).toLongLong();
    reminder.uuid = query.value(QStringLiteral("uuid")).toString();
    reminder.type = typeFromString(query.value(QStringLiteral("type")).toString());
    reminder.title = query.value(QStringLiteral("title")).toString();
    reminder.iconKey = query.value(QStringLiteral("icon_key")).toString();
    reminder.voiceType = voiceTypeFromString(query.value(QStringLiteral("voice_type")).toString());
    reminder.voiceText = query.value(QStringLiteral("voice_text")).toString();
    reminder.voiceAssetId = query.value(QStringLiteral("voice_asset_id")).toString();
    reminder.timeOfDay = QTime::fromString(query.value(QStringLiteral("time_of_day")).toString(),
                                           QStringLiteral("HH:mm"));
    reminder.scheduledDate = QDate::fromString(
        query.value(QStringLiteral("scheduled_date")).toString(), Qt::ISODate);
    reminder.repeatRule = repeatFromString(query.value(QStringLiteral("repeat_rule")).toString());
    reminder.repeatIntervalMinutes = query.value(QStringLiteral("repeat_interval_minutes")).toInt();
    reminder.maxPresentationCount = query.value(QStringLiteral("max_repeat_count")).toInt();
    reminder.enabled = query.value(QStringLiteral("enabled")).toBool();
    reminder.revision = query.value(QStringLiteral("revision")).toInt();
    reminder.createdAt = dateTimeFromDatabase(query.value(QStringLiteral("created_at")));
    reminder.updatedAt = dateTimeFromDatabase(query.value(QStringLiteral("updated_at")));
    return reminder;
}

ReminderOccurrence occurrenceFromQuery(const QSqlQuery& query)
{
    ReminderOccurrence occurrence;
    occurrence.id = query.value(QStringLiteral("id")).toLongLong();
    occurrence.reminderId = query.value(QStringLiteral("reminder_id")).toLongLong();
    occurrence.scheduledAt = dateTimeFromDatabase(query.value(QStringLiteral("scheduled_at"))).toLocalTime();
    occurrence.status = statusFromString(query.value(QStringLiteral("status")).toString());
    occurrence.presentationCount = query.value(QStringLiteral("presentation_count")).toInt();
    occurrence.lastPresentedAt = dateTimeFromDatabase(
        query.value(QStringLiteral("last_presented_at"))).toLocalTime();
    occurrence.acknowledgedAt = dateTimeFromDatabase(
        query.value(QStringLiteral("acknowledged_at"))).toLocalTime();
    occurrence.completedAt = dateTimeFromDatabase(
        query.value(QStringLiteral("completed_at"))).toLocalTime();
    occurrence.ackSource = ackSourceFromString(query.value(QStringLiteral("ack_source")).toString());
    return occurrence;
}

ServiceResult failedResult(const QSqlQuery& query)
{
    return {false, query.lastError().text(), 0};
}

QString occurrencePriorityOrder()
{
    return QStringLiteral(
        "CASE status WHEN 'completed' THEN 5 WHEN 'acknowledged' THEN 4 "
        "WHEN 'presented' THEN 3 WHEN 'missed' THEN 2 ELSE 1 END DESC, id DESC");
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
    const QString now = dateTimeToDatabase(QDateTime::currentDateTimeUtc());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO reminders(uuid,type,title,icon_key,voice_type,voice_text,voice_asset_id,"
        "time_of_day,scheduled_date,repeat_rule,repeat_interval_minutes,max_repeat_count,"
        "enabled,revision,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,1,?,?)"));
    query.addBindValue(draft.uuid);
    query.addBindValue(typeToString(draft.type));
    query.addBindValue(draft.title);
    query.addBindValue(draft.iconKey);
    query.addBindValue(voiceTypeToString(draft.voiceType));
    query.addBindValue(draft.voiceText);
    query.addBindValue(draft.voiceAssetId.isEmpty()
        ? QStringLiteral("") : draft.voiceAssetId);
    query.addBindValue(draft.timeOfDay.toString(QStringLiteral("HH:mm")));
    query.addBindValue(draft.scheduledDate.toString(Qt::ISODate));
    query.addBindValue(repeatToString(draft.repeatRule));
    query.addBindValue(draft.repeatIntervalMinutes);
    query.addBindValue(draft.maxPresentationCount);
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
        "UPDATE reminders SET type=?,title=?,icon_key=?,voice_type=?,voice_text=?,voice_asset_id=?,"
        "time_of_day=?,scheduled_date=?,repeat_rule=?,repeat_interval_minutes=?,max_repeat_count=?,"
        "enabled=?,revision=revision+1,updated_at=? WHERE id=? AND revision=?"));
    query.addBindValue(typeToString(draft.type));
    query.addBindValue(draft.title);
    query.addBindValue(draft.iconKey);
    query.addBindValue(voiceTypeToString(draft.voiceType));
    query.addBindValue(draft.voiceText);
    query.addBindValue(draft.voiceAssetId.isEmpty()
        ? QStringLiteral("") : draft.voiceAssetId);
    query.addBindValue(draft.timeOfDay.toString(QStringLiteral("HH:mm")));
    query.addBindValue(draft.scheduledDate.toString(Qt::ISODate));
    query.addBindValue(repeatToString(draft.repeatRule));
    query.addBindValue(draft.repeatIntervalMinutes);
    query.addBindValue(draft.maxPresentationCount);
    query.addBindValue(draft.enabled);
    query.addBindValue(dateTimeToDatabase(QDateTime::currentDateTimeUtc()));
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

ServiceResult ReminderRepository::createOccurrence(ReminderId reminderId,
                                                    const QDateTime& scheduledAt)
{
    bool found = false;
    QString error;
    const ReminderOccurrence existing = occurrenceForSchedule(
        reminderId, scheduledAt, &found, &error);
    if (!error.isEmpty())
        return {false, error, 0};
    if (found)
        return {true, {}, existing.id};

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO reminder_events(reminder_id,scheduled_at,status,completed_at,"
        "presentation_count,last_presented_at,acknowledged_at,ack_source) "
        "VALUES(?,?,'pending',NULL,0,NULL,NULL,'none')"));
    query.addBindValue(reminderId);
    query.addBindValue(dateTimeToDatabase(scheduledAt));
    if (!query.exec())
        return failedResult(query);
    return {true, {}, query.lastInsertId().toLongLong()};
}

ReminderOccurrence ReminderRepository::occurrence(ReminderEventId id, bool* found,
                                                  QString* error) const
{
    if (found)
        *found = false;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT * FROM reminder_events WHERE id=?"));
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
    return occurrenceFromQuery(query);
}

ReminderOccurrence ReminderRepository::occurrenceForSchedule(
    ReminderId reminderId, const QDateTime& scheduledAt, bool* found, QString* error) const
{
    if (found)
        *found = false;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT * FROM reminder_events WHERE reminder_id=? AND scheduled_at>=? AND scheduled_at<? ORDER BY ")
        + occurrencePriorityOrder() + QStringLiteral(" LIMIT 1"));
    query.addBindValue(reminderId);
    query.addBindValue(dateTimeToDatabase(scheduledAt));
    query.addBindValue(dateTimeToDatabase(scheduledAt.addSecs(1)));
    if (!query.exec()) {
        if (error)
            *error = query.lastError().text();
        return {};
    }
    if (!query.next())
        return {};
    if (found)
        *found = true;
    return occurrenceFromQuery(query);
}

ReminderOccurrence ReminderRepository::latestOccurrenceForDate(
    ReminderId reminderId, const QDate& date, bool* found, QString* error) const
{
    if (found)
        *found = false;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT * FROM reminder_events WHERE reminder_id=? AND scheduled_at>=? AND scheduled_at<? ORDER BY ")
        + occurrencePriorityOrder() + QStringLiteral(" LIMIT 1"));
    query.addBindValue(reminderId);
    query.addBindValue(dateTimeToDatabase(QDateTime(date.startOfDay())));
    query.addBindValue(dateTimeToDatabase(QDateTime(date.addDays(1).startOfDay())));
    if (!query.exec()) {
        if (error)
            *error = query.lastError().text();
        return {};
    }
    if (!query.next())
        return {};
    if (found)
        *found = true;
    return occurrenceFromQuery(query);
}

bool ReminderRepository::markPresented(ReminderEventId id, const QDateTime& presentedAt,
                                       ReminderOccurrence* updated, QString* error)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE reminder_events SET status='presented',presentation_count=presentation_count+1,"
        "last_presented_at=? WHERE id=? AND status IN ('pending','presented')"));
    query.addBindValue(dateTimeToDatabase(presentedAt));
    query.addBindValue(id);
    if (!query.exec() || query.numRowsAffected() != 1) {
        if (error)
            *error = query.lastError().isValid()
                ? query.lastError().text() : QStringLiteral("提醒 occurrence 状态已变更");
        return false;
    }
    if (updated) {
        bool found = false;
        *updated = occurrence(id, &found, error);
        return found;
    }
    return true;
}

bool ReminderRepository::setOccurrenceStatus(ReminderEventId id,
                                             ReminderOccurrenceStatus status,
                                             ReminderAckSource source,
                                             const QDateTime& changedAt,
                                             QString* error)
{
    QSqlQuery query(m_database);
    QString sql = QStringLiteral("UPDATE reminder_events SET status=?,ack_source=?");
    if (status == ReminderOccurrenceStatus::Acknowledged)
        sql += QStringLiteral(",acknowledged_at=?");
    else if (status == ReminderOccurrenceStatus::Completed)
        sql += QStringLiteral(",completed_at=?,acknowledged_at=COALESCE(acknowledged_at,?)");
    sql += QStringLiteral(" WHERE id=?");
    query.prepare(sql);
    query.addBindValue(statusToString(status));
    query.addBindValue(ackSourceToString(source));
    if (status == ReminderOccurrenceStatus::Acknowledged) {
        query.addBindValue(dateTimeToDatabase(changedAt));
    } else if (status == ReminderOccurrenceStatus::Completed) {
        query.addBindValue(dateTimeToDatabase(changedAt));
        query.addBindValue(dateTimeToDatabase(changedAt));
    }
    query.addBindValue(id);
    if (!query.exec() || query.numRowsAffected() != 1) {
        if (error)
            *error = query.lastError().isValid()
                ? query.lastError().text() : QStringLiteral("提醒 occurrence 不存在");
        return false;
    }
    return true;
}

bool ReminderRepository::hasEventForDate(ReminderId id, const QDate& date,
                                         QString* error) const
{
    bool found = false;
    latestOccurrenceForDate(id, date, &found, error);
    return found;
}

ReminderOccurrenceStatus ReminderRepository::statusForDate(ReminderId id,
                                                           const QDate& date,
                                                           QString* error) const
{
    bool found = false;
    const ReminderOccurrence occurrence = latestOccurrenceForDate(id, date, &found, error);
    return found ? occurrence.status : ReminderOccurrenceStatus::Pending;
}
