#include "ReminderService.h"

#include "data/ReminderRepository.h"

#include <QDir>
#include <QRegularExpression>
#include <QUuid>

namespace {
QString defaultTitle(ReminderType type)
{
    switch (type) {
    case ReminderType::Medicine: return QStringLiteral("按时用药");
    case ReminderType::Water: return QStringLiteral("记得喝水");
    case ReminderType::Other: return QStringLiteral("日常提醒");
    }
    return QStringLiteral("日常提醒");
}

bool validIconKey(const QString& key)
{
    static const QRegularExpression expression(
        QStringLiteral("^[A-Za-z0-9_-]{1,64}$"));
    return expression.match(key).hasMatch();
}

bool validAssetId(const QString& assetId)
{
    if (assetId.isEmpty())
        return true;
    if (assetId.size() > 128 || QDir::isAbsolutePath(assetId)
        || assetId.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)) {
        return false;
    }
    static const QRegularExpression expression(
        QStringLiteral("^[A-Za-z0-9_.-]+$"));
    return expression.match(assetId).hasMatch();
}
}

ReminderService::ReminderService(ReminderRepository* repository, QObject* parent)
    : ReminderService(repository,
          [] { return QDateTime::currentDateTime(); }, parent)
{
}

ReminderService::ReminderService(ReminderRepository* repository, Clock clock,
                                 QObject* parent)
    : QObject(parent), m_repository(repository), m_clock(std::move(clock))
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
    const QDateTime current = now();
    for (Reminder& reminder : result) {
        if (!reminder.enabled) {
            reminder.status = ReminderOccurrenceStatus::Disabled;
            continue;
        }
        reminder.status = m_repository->statusForDate(reminder.id, current.date(), error);
        const bool occurredWithoutDelivery = reminder.status == ReminderOccurrenceStatus::Pending
            && !m_repository->hasEventForDate(reminder.id, current.date(), error)
            && ((appliesOnDate(reminder, current.date()) && reminder.timeOfDay < current.time())
                || (reminder.repeatRule == ReminderRepeatRule::Once
                    && reminder.scheduledDate < current.date()));
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
        return {false, QStringLiteral("提醒内容不能超过 40 个字"), 0};
    if (!validated.timeOfDay.isValid())
        return {false, QStringLiteral("请选择有效的提醒时间"), 0};
    if (!validated.scheduledDate.isValid())
        validated.scheduledDate = now().date();
    if (validated.repeatRule == ReminderRepeatRule::Once
        && QDateTime(validated.scheduledDate, validated.timeOfDay) <= now()) {
        return {false, QStringLiteral("单次提醒时间必须晚于当前时间"), 0};
    }

    validated.iconKey = validated.iconKey.trimmed().toLower();
    if (validated.iconKey.isEmpty())
        validated.iconKey = defaultReminderIconKey(validated.type);
    if (!validIconKey(validated.iconKey))
        return {false, QStringLiteral("提醒图标标识无效"), 0};
    validated.voiceText = validated.voiceText.simplified();
    if (validated.voiceText.isEmpty())
        validated.voiceText = validated.title;
    if (validated.voiceText.size() > 200)
        return {false, QStringLiteral("提醒语音文本不能超过 200 个字"), 0};
    validated.voiceAssetId = validated.voiceAssetId.trimmed();
    if (!validAssetId(validated.voiceAssetId))
        return {false, QStringLiteral("录音资源必须使用安全的资源标识，不能保存本地文件路径"), 0};
    if (validated.repeatIntervalMinutes < 1 || validated.repeatIntervalMinutes > 24 * 60)
        return {false, QStringLiteral("重复提醒间隔必须在 1 到 1440 分钟之间"), 0};
    if (validated.maxPresentationCount < 1 || validated.maxPresentationCount > 10)
        return {false, QStringLiteral("最多展示次数必须在 1 到 10 次之间"), 0};

    if (validated.id == 0) {
        QUuid uuid;
        if (validated.uuid.isEmpty()) {
            uuid = QUuid::createUuid();
        } else {
            uuid = QUuid(validated.uuid);
            if (uuid.isNull())
                return {false, QStringLiteral("提醒 UUID 格式无效"), 0};
        }
        validated.uuid = uuid.toString(QUuid::WithoutBraces);
    } else {
        bool found = false;
        QString error;
        const Reminder existing = m_repository->find(validated.id, &found, &error);
        if (!error.isEmpty())
            return {false, error, 0};
        if (!found)
            return {false, QStringLiteral("提醒不存在"), 0};
        validated.uuid = existing.uuid;
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

ServiceResult ReminderService::remove(ReminderId id)
{
    const ServiceResult result = m_repository->remove(id);
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
    const QDate today = now().date();
    if (!appliesOnDate(current, today))
        return {false, QStringLiteral("这条提醒今天不需要完成"), 0};

    ReminderOccurrence occurrence = m_repository->latestOccurrenceForDate(
        id, today, &found, &error);
    if (!error.isEmpty())
        return {false, error, 0};
    if (!found) {
        const ServiceResult created = m_repository->createOccurrence(
            id, QDateTime(today, current.timeOfDay));
        if (!created.success)
            return created;
        occurrence.id = created.id;
        occurrence.status = ReminderOccurrenceStatus::Pending;
    }
    if (occurrence.status == ReminderOccurrenceStatus::Completed)
        return {false, QStringLiteral("这条提醒今天已经完成"), 0};
    return transitionOccurrence(occurrence.id, ReminderOccurrenceStatus::Completed,
                                ReminderAckSource::Touch);
}

ServiceResult ReminderService::acknowledgeOccurrence(ReminderEventId eventId,
                                                     ReminderAckSource source)
{
    return transitionOccurrence(eventId, ReminderOccurrenceStatus::Acknowledged,
                                source);
}

ServiceResult ReminderService::completeOccurrence(ReminderEventId eventId,
                                                  ReminderAckSource source)
{
    return transitionOccurrence(eventId, ReminderOccurrenceStatus::Completed,
                                source);
}

int ReminderService::presentationDurationMs() const
{
    return ReminderDefaults::PresentationDurationMs;
}

void ReminderService::start()
{
    checkDueReminders();
}

void ReminderService::stop()
{
    m_scheduler.stop();
}

void ReminderService::checkNow()
{
    checkDueReminders();
}

bool ReminderService::appliesOnDate(const Reminder& reminder, const QDate& date) const
{
    if (reminder.repeatRule == ReminderRepeatRule::Once)
        return date == reminder.scheduledDate;
    if (reminder.repeatRule == ReminderRepeatRule::Weekdays)
        return date.dayOfWeek() >= Qt::Monday && date.dayOfWeek() <= Qt::Friday;
    return true;
}

QDateTime ReminderService::nextOccurrence(const Reminder& reminder,
                                          const QDateTime& after) const
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

QDateTime ReminderService::now() const
{
    return m_clock ? m_clock() : QDateTime::currentDateTime();
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
    const QDateTime current = now();
    QDateTime nearest;
    bool hasEnabledReminder = false;
    for (const Reminder& reminder : all) {
        if (!reminder.enabled)
            continue;
        hasEnabledReminder = true;
        const QDateTime occurrence = nextOccurrence(reminder, current);
        if (occurrence.isValid() && (!nearest.isValid() || occurrence < nearest))
            nearest = occurrence;
    }
    if (!hasEnabledReminder)
        return;
    const qint64 untilNearest = nearest.isValid()
        ? qMax<qint64>(1, current.msecsTo(nearest))
        : ReminderDefaults::SchedulerRecheckMs;
    m_scheduler.start(static_cast<int>(qMin<qint64>(
        untilNearest, ReminderDefaults::SchedulerRecheckMs)));
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

    const QDateTime current = now();
    bool changed = false;
    for (const Reminder& reminder : all) {
        if (!reminder.enabled || !appliesOnDate(reminder, current.date()))
            continue;
        const QDateTime scheduledAt(current.date(), reminder.timeOfDay);
        if (scheduledAt > current)
            continue;

        bool found = false;
        ReminderOccurrence occurrence = m_repository->occurrenceForSchedule(
            reminder.id, scheduledAt, &found, &error);
        if (!error.isEmpty()) {
            emit errorOccurred(error);
            error.clear();
            continue;
        }
        if (!found) {
            const ServiceResult created = m_repository->createOccurrence(
                reminder.id, scheduledAt);
            if (!created.success) {
                emit errorOccurred(created.error);
                continue;
            }
            occurrence = m_repository->occurrence(created.id, &found, &error);
            if (!found || !error.isEmpty()) {
                emit errorOccurred(error.isEmpty()
                    ? QStringLiteral("无法读取新建的提醒 occurrence") : error);
                error.clear();
                continue;
            }
            changed = true;
        }

        if (occurrence.status == ReminderOccurrenceStatus::Pending) {
            present(reminder, occurrence, current, &changed);
            continue;
        }
        if (occurrence.status != ReminderOccurrenceStatus::Presented)
            continue;

        const QDateTime repeatAt = occurrence.lastPresentedAt.addSecs(
            reminder.repeatIntervalMinutes * 60);
        if (repeatAt > current)
            continue;
        if (occurrence.presentationCount < reminder.maxPresentationCount) {
            present(reminder, occurrence, current, &changed);
        } else if (!m_repository->setOccurrenceStatus(
                       occurrence.id, ReminderOccurrenceStatus::Missed,
                       ReminderAckSource::None, current, &error)) {
            emit errorOccurred(error);
            error.clear();
        } else {
            changed = true;
        }
    }
    if (changed)
        emit remindersChanged();
    scheduleNext();
}

void ReminderService::present(const Reminder& reminder,
                              const ReminderOccurrence& occurrence,
                              const QDateTime& presentedAt, bool* changed)
{
    QString error;
    ReminderOccurrence updated;
    if (!m_repository->markPresented(occurrence.id, presentedAt, &updated, &error)) {
        emit errorOccurred(error);
        return;
    }
    if (changed)
        *changed = true;
    emit reminderPresentationRequested({reminder, updated});
}

ServiceResult ReminderService::transitionOccurrence(
    ReminderEventId eventId, ReminderOccurrenceStatus target,
    ReminderAckSource source)
{
    bool found = false;
    QString error;
    const ReminderOccurrence occurrence = m_repository->occurrence(
        eventId, &found, &error);
    if (!error.isEmpty())
        return {false, error, 0};
    if (!found)
        return {false, QStringLiteral("提醒 occurrence 不存在"), 0};
    if (occurrence.status == ReminderOccurrenceStatus::Completed)
        return target == ReminderOccurrenceStatus::Completed
            ? ServiceResult{true, {}, eventId}
            : ServiceResult{false, QStringLiteral("已完成的提醒不能再确认"), 0};
    if (occurrence.status == ReminderOccurrenceStatus::Acknowledged) {
        if (target == ReminderOccurrenceStatus::Acknowledged)
            return {true, {}, eventId};
        if (target != ReminderOccurrenceStatus::Completed)
            return {false, QStringLiteral("已确认的提醒不能变更为该状态"), 0};
    }
    if (occurrence.status == ReminderOccurrenceStatus::Missed)
        return {false, QStringLiteral("已错过的提醒不能在弹窗中确认"), 0};
    if (!m_repository->setOccurrenceStatus(eventId, target, source, now(), &error))
        return {false, error, 0};
    emit remindersChanged();
    return {true, {}, eventId};
}
