#pragma once

#include "model/ReminderModels.h"

#include <QSqlDatabase>

class ReminderRepository final {
public:
    explicit ReminderRepository(const QSqlDatabase& database);

    QList<Reminder> all(QString* error = nullptr) const;
    Reminder find(ReminderId id, bool* found = nullptr, QString* error = nullptr) const;
    ServiceResult insert(const ReminderDraft& draft);
    ServiceResult update(ReminderId id, const ReminderDraft& draft);
    ServiceResult remove(ReminderId id, int expectedRevision = -1);
    bool addEvent(ReminderId id, const QDateTime& scheduledAt,
                  ReminderOccurrenceStatus status, QString* error = nullptr);
    bool hasEventForDate(ReminderId id, const QDate& date,
                         QString* error = nullptr) const;
    ReminderOccurrenceStatus statusForDate(ReminderId id, const QDate& date,
                                           QString* error = nullptr) const;

private:
    QSqlDatabase m_database;
};
