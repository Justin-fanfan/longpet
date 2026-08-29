#pragma once

#include "model/ReminderModels.h"

#include <QObject>
#include <QTimer>

class ReminderRepository;

class ReminderService final : public QObject {
    Q_OBJECT

public:
    explicit ReminderService(ReminderRepository* repository, QObject* parent = nullptr);

    QList<Reminder> reminders(QString* error = nullptr) const;
    Reminder reminder(ReminderId id, bool* found = nullptr, QString* error = nullptr) const;
    ServiceResult save(const ReminderDraft& draft);
    ServiceResult remove(ReminderId id, int expectedRevision = -1);
    ServiceResult markCompleted(ReminderId id);
    void start();
    void stop();

signals:
    void remindersChanged();
    void reminderTriggered(const Reminder& reminder);
    void errorOccurred(const QString& message);

private:
    bool appliesOnDate(const Reminder& reminder, const QDate& date) const;
    QDateTime nextOccurrence(const Reminder& reminder, const QDateTime& after) const;
    void scheduleNext();
    void checkDueReminders();

    ReminderRepository* m_repository = nullptr;
    QTimer m_scheduler;
};
