#pragma once

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QTime>

using ReminderId = qint64;

enum class ReminderType {
    Medicine,
    Water,
    Other
};

enum class ReminderRepeatRule {
    Daily,
    Weekdays,
    Once
};

enum class ReminderOccurrenceStatus {
    Pending,
    Completed,
    Missed,
    Disabled
};

struct Reminder {
    ReminderId id = 0;
    ReminderType type = ReminderType::Other;
    QString title;
    QTime timeOfDay;
    QDate scheduledDate;
    ReminderRepeatRule repeatRule = ReminderRepeatRule::Daily;
    bool enabled = true;
    int revision = 0;
    QDateTime createdAt;
    QDateTime updatedAt;
    ReminderOccurrenceStatus status = ReminderOccurrenceStatus::Pending;
};

struct ReminderDraft {
    ReminderId id = 0;
    ReminderType type = ReminderType::Medicine;
    QString title;
    QTime timeOfDay = QTime(8, 0);
    QDate scheduledDate = QDate::currentDate();
    ReminderRepeatRule repeatRule = ReminderRepeatRule::Daily;
    bool enabled = true;
    int expectedRevision = 0;
};

struct ServiceResult {
    bool success = false;
    QString error;
    ReminderId id = 0;
};

struct CareSummary {
    int waterCompleted = 0;
    int waterGoal = 8;
    int medicineCompleted = 0;
    int medicineTotal = 0;
    int activityMinutes = 0;
    int interactionCount = 0;
    QDateTime lastUpdated;
};

Q_DECLARE_METATYPE(Reminder)
Q_DECLARE_METATYPE(ReminderDraft)
