#pragma once

#include "model/ReminderModels.h"

#include <QObject>
#include <QTimer>

#include <functional>

class ReminderRepository;

class ReminderService final : public QObject {
    Q_OBJECT

public:
    using Clock = std::function<QDateTime()>;

    explicit ReminderService(ReminderRepository* repository, QObject* parent = nullptr);
    ReminderService(ReminderRepository* repository, Clock clock,
                    QObject* parent = nullptr);

    QList<Reminder> reminders(QString* error = nullptr) const;
    Reminder reminder(ReminderId id, bool* found = nullptr,
                      QString* error = nullptr) const;
    ServiceResult save(const ReminderDraft& draft);
    ServiceResult remove(ReminderId id);
    ServiceResult markCompleted(ReminderId id);
    ServiceResult acknowledgeOccurrence(ReminderEventId eventId,
                                        ReminderAckSource source);
    ServiceResult completeOccurrence(ReminderEventId eventId,
                                     ReminderAckSource source);
    int presentationDurationMs() const;

    void start();
    void stop();
    void checkNow();
    void suspendScheduling();
    void resumeScheduling();
    bool isSchedulingSuspended() const;
    void requestDiagnosticPresentation();

signals:
    void remindersChanged();
    void reminderPresentationRequested(const ReminderPresentation& presentation);
    void errorOccurred(const QString& message);

private:
    bool appliesOnDate(const Reminder& reminder, const QDate& date) const;
    QDateTime nextOccurrence(const Reminder& reminder, const QDateTime& after) const;
    QDateTime now() const;
    void scheduleNext();
    void checkDueReminders();
    void present(const Reminder& reminder, const ReminderOccurrence& occurrence,
                 const QDateTime& presentedAt, bool* changed);
    ServiceResult transitionOccurrence(ReminderEventId eventId,
                                       ReminderOccurrenceStatus target,
                                       ReminderAckSource source);

    ReminderRepository* m_repository = nullptr;
    Clock m_clock;
    QTimer m_scheduler;
    bool m_started = false;
    bool m_schedulingSuspended = false;
};
