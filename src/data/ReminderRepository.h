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
    ServiceResult remove(ReminderId id);

    ServiceResult createOccurrence(ReminderId reminderId, const QDateTime& scheduledAt);
    ReminderOccurrence occurrence(ReminderEventId id, bool* found = nullptr,
                                  QString* error = nullptr) const;
    ReminderOccurrence occurrenceForSchedule(ReminderId reminderId,
                                             const QDateTime& scheduledAt,
                                             bool* found = nullptr,
                                             QString* error = nullptr) const;
    ReminderOccurrence latestOccurrenceForDate(ReminderId reminderId, const QDate& date,
                                               bool* found = nullptr,
                                               QString* error = nullptr) const;
    bool markPresented(ReminderEventId id, const QDateTime& presentedAt,
                       ReminderOccurrence* updated = nullptr,
                       QString* error = nullptr);
    bool setOccurrenceStatus(ReminderEventId id, ReminderOccurrenceStatus status,
                             ReminderAckSource source, const QDateTime& changedAt,
                             QString* error = nullptr);

    bool hasEventForDate(ReminderId id, const QDate& date,
                         QString* error = nullptr) const;
    ReminderOccurrenceStatus statusForDate(ReminderId id, const QDate& date,
                                           QString* error = nullptr) const;

private:
    QSqlDatabase m_database;
};
