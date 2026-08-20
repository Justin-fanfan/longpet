#pragma once

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QTime>

using ReminderId = qint64;
using ReminderEventId = qint64;

namespace ReminderDefaults {
inline constexpr int PresentationDurationMs = 30'000;
inline constexpr int RepeatIntervalMinutes = 5;
inline constexpr int MaxPresentationCount = 3;
inline constexpr int SchedulerRecheckMs = 60'000;
}

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
    Presented,
    Acknowledged,
    Completed,
    Missed,
    Disabled
};

enum class ReminderVoiceType {
    None,
    Tts,
    AudioAsset
};

enum class ReminderAckSource {
    None,
    Touch,
    Voice,
    Family
};

enum class ReminderConfirmationSemantic {
    Acknowledge,
    Complete
};

struct Reminder {
    ReminderId id = 0;
    QString uuid;
    ReminderType type = ReminderType::Other;
    QString title;
    QString iconKey;
    ReminderVoiceType voiceType = ReminderVoiceType::None;
    QString voiceText;
    QString voiceAssetId;
    QTime timeOfDay;
    QDate scheduledDate;
    ReminderRepeatRule repeatRule = ReminderRepeatRule::Daily;
    int repeatIntervalMinutes = ReminderDefaults::RepeatIntervalMinutes;
    int maxPresentationCount = ReminderDefaults::MaxPresentationCount;
    bool enabled = true;
    int revision = 0;
    QDateTime createdAt;
    QDateTime updatedAt;
    ReminderOccurrenceStatus status = ReminderOccurrenceStatus::Pending;
};

struct ReminderDraft {
    ReminderId id = 0;
    QString uuid;
    ReminderType type = ReminderType::Medicine;
    QString title;
    QString iconKey;
    ReminderVoiceType voiceType = ReminderVoiceType::None;
    QString voiceText;
    QString voiceAssetId;
    QTime timeOfDay = QTime(8, 0);
    QDate scheduledDate = QDate::currentDate();
    ReminderRepeatRule repeatRule = ReminderRepeatRule::Daily;
    int repeatIntervalMinutes = ReminderDefaults::RepeatIntervalMinutes;
    int maxPresentationCount = ReminderDefaults::MaxPresentationCount;
    bool enabled = true;
    int expectedRevision = 0;
};

struct ReminderOccurrence {
    ReminderEventId id = 0;
    ReminderId reminderId = 0;
    QDateTime scheduledAt;
    ReminderOccurrenceStatus status = ReminderOccurrenceStatus::Pending;
    int presentationCount = 0;
    QDateTime lastPresentedAt;
    QDateTime acknowledgedAt;
    QDateTime completedAt;
    ReminderAckSource ackSource = ReminderAckSource::None;
};

struct ReminderPresentation {
    Reminder reminder;
    ReminderOccurrence occurrence;
    bool diagnostic = false;
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

QString defaultReminderIconKey(ReminderType type);
QString reminderIconResourcePath(const QString& iconKey);

Q_DECLARE_METATYPE(Reminder)
Q_DECLARE_METATYPE(ReminderDraft)
Q_DECLARE_METATYPE(ReminderOccurrence)
Q_DECLARE_METATYPE(ReminderPresentation)
Q_DECLARE_METATYPE(ReminderAckSource)
Q_DECLARE_METATYPE(ReminderConfirmationSemantic)
