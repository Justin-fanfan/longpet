#include "VoiceToolRegistry.h"

#include "services/ReminderService.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>

namespace {
QJsonObject stringProperty(const QString& description,
                           const QStringList& values = {})
{
    QJsonObject property {
        {QStringLiteral("type"), QStringLiteral("string")},
        {QStringLiteral("description"), description}
    };
    if (!values.isEmpty())
        property.insert(QStringLiteral("enum"), QJsonArray::fromStringList(values));
    return property;
}

QJsonObject objectSchema(const QJsonObject& properties,
                         const QStringList& required = {})
{
    return QJsonObject {
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"), properties},
        {QStringLiteral("required"), QJsonArray::fromStringList(required)},
        {QStringLiteral("additionalProperties"), false}
    };
}

AiToolExecutionResult jsonResult(bool success, const QJsonObject& payload,
                                 const QString& userMessage = {})
{
    QJsonObject root = payload;
    root.insert(QStringLiteral("ok"), success);
    return {success,
            QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)),
            userMessage};
}

QString reminderTypeName(ReminderType type)
{
    switch (type) {
    case ReminderType::Medicine: return QStringLiteral("medicine");
    case ReminderType::Water: return QStringLiteral("water");
    case ReminderType::Other: return QStringLiteral("other");
    }
    return QStringLiteral("other");
}

QString repeatName(ReminderRepeatRule repeat)
{
    switch (repeat) {
    case ReminderRepeatRule::Daily: return QStringLiteral("daily");
    case ReminderRepeatRule::Weekdays: return QStringLiteral("weekdays");
    case ReminderRepeatRule::Once: return QStringLiteral("once");
    }
    return QStringLiteral("daily");
}
}

VoiceToolRegistry::VoiceToolRegistry(ReminderService* reminderService,
                                     QObject* parent)
    : QObject(parent), m_reminderService(reminderService)
{
}

QList<AiToolDefinition> VoiceToolRegistry::definitions() const
{
    return {
        {QStringLiteral("create_reminder"),
         QStringLiteral("创建一个本地提醒。时间使用 24 小时 HH:mm；单次提醒可给出 yyyy-MM-dd 日期。"),
         objectSchema({
             {QStringLiteral("title"), stringProperty(QStringLiteral("提醒内容"))},
             {QStringLiteral("time"), stringProperty(QStringLiteral("24 小时时间，例如 08:30"))},
             {QStringLiteral("date"), stringProperty(QStringLiteral("可选日期 yyyy-MM-dd"))},
             {QStringLiteral("repeat"), stringProperty(
                 QStringLiteral("重复规则"), {QStringLiteral("daily"),
                 QStringLiteral("weekdays"), QStringLiteral("once")})},
             {QStringLiteral("type"), stringProperty(
                 QStringLiteral("提醒类型"), {QStringLiteral("medicine"),
                 QStringLiteral("water"), QStringLiteral("other")})}
         }, {QStringLiteral("title"), QStringLiteral("time")})},
        {QStringLiteral("list_reminders"),
         QStringLiteral("列出设备中的提醒及其 ID、时间、日期和重复规则。"),
         objectSchema({})},
        {QStringLiteral("delete_reminder"),
         QStringLiteral("按明确的提醒 ID 删除一个本地提醒。应先通过 list_reminders 确认目标。"),
         objectSchema({
             {QStringLiteral("id"), QJsonObject {
                 {QStringLiteral("type"), QStringLiteral("integer")},
                 {QStringLiteral("description"), QStringLiteral("提醒 ID")}
             }}
         }, {QStringLiteral("id")})},
        {QStringLiteral("get_current_time"),
         QStringLiteral("获取 LongPet 设备当前的本地日期和时间。"),
         objectSchema({})},
        {QStringLiteral("open_page"),
         QStringLiteral("在 LongPet 上打开一个已有页面。"),
         objectSchema({
             {QStringLiteral("page"), stringProperty(
                 QStringLiteral("页面名称"), {QStringLiteral("home"),
                 QStringLiteral("reminders"), QStringLiteral("care"),
                 QStringLiteral("settings"), QStringLiteral("companion")})}
         }, {QStringLiteral("page")})}
    };
}

AiToolExecutionResult VoiceToolRegistry::execute(const AiToolCall& call)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        call.argumentsJson.trimmed().isEmpty() ? QByteArrayLiteral("{}")
                                                : call.argumentsJson.toUtf8(),
        &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return jsonResult(false, {{QStringLiteral("error"),
                                  QStringLiteral("工具参数不是有效 JSON 对象")}},
                          QStringLiteral("语音指令参数不完整"));
    }
    const QJsonObject arguments = document.object();
    if (call.name == QStringLiteral("create_reminder"))
        return createReminder(arguments);
    if (call.name == QStringLiteral("list_reminders"))
        return listReminders();
    if (call.name == QStringLiteral("delete_reminder"))
        return deleteReminder(arguments);
    if (call.name == QStringLiteral("get_current_time"))
        return currentTime();
    if (call.name == QStringLiteral("open_page"))
        return openPage(arguments);
    return jsonResult(false, {{QStringLiteral("error"),
                              QStringLiteral("工具不在允许列表中")}},
                      QStringLiteral("这项语音操作暂不支持"));
}

AiToolExecutionResult VoiceToolRegistry::createReminder(
    const QJsonObject& arguments)
{
    if (!m_reminderService)
        return jsonResult(false, {{QStringLiteral("error"), QStringLiteral("提醒服务不可用")}});

    ReminderDraft draft;
    draft.title = arguments.value(QStringLiteral("title")).toString().simplified();
    draft.timeOfDay = QTime::fromString(
        arguments.value(QStringLiteral("time")).toString(), QStringLiteral("HH:mm"));
    const QString dateText = arguments.value(QStringLiteral("date")).toString();
    draft.scheduledDate = dateText.isEmpty()
        ? QDate::currentDate()
        : QDate::fromString(dateText, QStringLiteral("yyyy-MM-dd"));

    const QString type = arguments.value(QStringLiteral("type")).toString(
        QStringLiteral("other"));
    if (type == QStringLiteral("medicine"))
        draft.type = ReminderType::Medicine;
    else if (type == QStringLiteral("water"))
        draft.type = ReminderType::Water;
    else if (type == QStringLiteral("other"))
        draft.type = ReminderType::Other;
    else
        return jsonResult(false, {{QStringLiteral("error"), QStringLiteral("未知提醒类型")}});

    const QString repeat = arguments.value(QStringLiteral("repeat")).toString(
        QStringLiteral("daily"));
    if (repeat == QStringLiteral("daily"))
        draft.repeatRule = ReminderRepeatRule::Daily;
    else if (repeat == QStringLiteral("weekdays"))
        draft.repeatRule = ReminderRepeatRule::Weekdays;
    else if (repeat == QStringLiteral("once"))
        draft.repeatRule = ReminderRepeatRule::Once;
    else
        return jsonResult(false, {{QStringLiteral("error"), QStringLiteral("未知重复规则")}});

    if (draft.repeatRule == ReminderRepeatRule::Once && dateText.isEmpty()
        && draft.timeOfDay.isValid() && draft.timeOfDay <= QTime::currentTime()) {
        draft.scheduledDate = QDate::currentDate().addDays(1);
    }
    const ServiceResult result = m_reminderService->save(draft);
    if (!result.success) {
        return jsonResult(false, {{QStringLiteral("error"), result.error}},
                          QStringLiteral("提醒创建失败：%1").arg(result.error));
    }
    return jsonResult(true, {
        {QStringLiteral("id"), result.id},
        {QStringLiteral("title"), draft.title},
        {QStringLiteral("time"), draft.timeOfDay.toString(QStringLiteral("HH:mm"))},
        {QStringLiteral("date"), draft.scheduledDate.toString(Qt::ISODate)},
        {QStringLiteral("repeat"), repeatName(draft.repeatRule)}
    });
}

AiToolExecutionResult VoiceToolRegistry::listReminders() const
{
    if (!m_reminderService)
        return jsonResult(false, {{QStringLiteral("error"), QStringLiteral("提醒服务不可用")}});
    QString error;
    const QList<Reminder> reminders = m_reminderService->reminders(&error);
    if (!error.isEmpty())
        return jsonResult(false, {{QStringLiteral("error"), error}});
    QJsonArray items;
    for (const Reminder& reminder : reminders) {
        items.append(QJsonObject {
            {QStringLiteral("id"), reminder.id},
            {QStringLiteral("title"), reminder.title},
            {QStringLiteral("time"), reminder.timeOfDay.toString(QStringLiteral("HH:mm"))},
            {QStringLiteral("date"), reminder.scheduledDate.toString(Qt::ISODate)},
            {QStringLiteral("repeat"), repeatName(reminder.repeatRule)},
            {QStringLiteral("type"), reminderTypeName(reminder.type)},
            {QStringLiteral("enabled"), reminder.enabled}
        });
    }
    return jsonResult(true, {{QStringLiteral("reminders"), items},
                             {QStringLiteral("count"), items.size()}});
}

AiToolExecutionResult VoiceToolRegistry::deleteReminder(
    const QJsonObject& arguments)
{
    if (!m_reminderService)
        return jsonResult(false, {{QStringLiteral("error"), QStringLiteral("提醒服务不可用")}});
    const qint64 id = arguments.value(QStringLiteral("id")).toInteger();
    if (id <= 0)
        return jsonResult(false, {{QStringLiteral("error"), QStringLiteral("提醒 ID 无效")}});
    const ServiceResult result = m_reminderService->remove(id);
    if (!result.success) {
        return jsonResult(false, {{QStringLiteral("error"), result.error}},
                          QStringLiteral("删除提醒失败：%1").arg(result.error));
    }
    return jsonResult(true, {{QStringLiteral("deleted_id"), id}});
}

AiToolExecutionResult VoiceToolRegistry::currentTime() const
{
    const QDateTime now = QDateTime::currentDateTime();
    return jsonResult(true, {
        {QStringLiteral("iso"), now.toString(Qt::ISODate)},
        {QStringLiteral("date"), now.date().toString(QStringLiteral("yyyy-MM-dd"))},
        {QStringLiteral("time"), now.time().toString(QStringLiteral("HH:mm"))},
        {QStringLiteral("weekday"), now.toString(QStringLiteral("dddd"))}
    });
}

AiToolExecutionResult VoiceToolRegistry::openPage(const QJsonObject& arguments)
{
    const QString page = arguments.value(QStringLiteral("page")).toString();
    const QSet<QString> allowed {QStringLiteral("home"), QStringLiteral("reminders"),
                                 QStringLiteral("care"), QStringLiteral("settings"),
                                 QStringLiteral("companion")};
    if (!allowed.contains(page))
        return jsonResult(false, {{QStringLiteral("error"), QStringLiteral("页面不在允许列表中")}});
    emit pageRequested(page);
    return jsonResult(true, {{QStringLiteral("page"), page}});
}
