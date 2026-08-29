#include "FamilyLinkController.h"

#include "services/FamilyLinkService.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QUrl>

#include <utility>

namespace {
QJsonValue dateTimeValue(const QDateTime& value)
{
    return value.isValid() ? QJsonValue(value.toUTC().toString(Qt::ISODate))
                           : QJsonValue(QJsonValue::Null);
}

QString reminderTypeName(ReminderType type)
{
    switch (type) {
    case ReminderType::Medicine:
        return QStringLiteral("medicine");
    case ReminderType::Water:
        return QStringLiteral("water");
    case ReminderType::Other:
        return QStringLiteral("other");
    }
    return QStringLiteral("other");
}

QString repeatRuleName(ReminderRepeatRule rule)
{
    switch (rule) {
    case ReminderRepeatRule::Daily:
        return QStringLiteral("daily");
    case ReminderRepeatRule::Weekdays:
        return QStringLiteral("weekdays");
    case ReminderRepeatRule::Once:
        return QStringLiteral("once");
    }
    return QStringLiteral("once");
}

QString occurrenceStatusName(ReminderOccurrenceStatus status)
{
    switch (status) {
    case ReminderOccurrenceStatus::Pending:
        return QStringLiteral("pending");
    case ReminderOccurrenceStatus::Completed:
        return QStringLiteral("completed");
    case ReminderOccurrenceStatus::Missed:
        return QStringLiteral("missed");
    case ReminderOccurrenceStatus::Disabled:
        return QStringLiteral("disabled");
    }
    return QStringLiteral("pending");
}

bool isKnownReadPath(const QString& path)
{
    return path == QStringLiteral("/api/v1/status")
        || path == QStringLiteral("/api/v1/settings")
        || path == QStringLiteral("/api/v1/reminders");
}
}

FamilyLinkController::FamilyLinkController(FamilyLinkService* service,
                                           FamilyLinkHttpAdapter* httpAdapter,
                                           QByteArray bearerToken)
    : m_service(service),
      m_httpAdapter(httpAdapter),
      m_bearerToken(std::move(bearerToken))
{
    if (m_httpAdapter) {
        m_httpAdapter->setRequestHandler(
            [this](const FamilyLinkHttpRequest& request) {
                return handleRequest(request);
            });
    }
}

FamilyLinkController::~FamilyLinkController()
{
    stop();
    if (m_httpAdapter)
        m_httpAdapter->setRequestHandler({});
}

bool FamilyLinkController::start(quint16 port, QHostAddress address, QString* error)
{
    if (!m_httpAdapter) {
        if (error)
            *error = QStringLiteral("FamilyLink HTTP Adapter 未初始化");
        return false;
    }
    return m_httpAdapter->start(port, error, address);
}

void FamilyLinkController::stop()
{
    if (m_httpAdapter)
        m_httpAdapter->stop();
}

quint16 FamilyLinkController::port() const
{
    return m_httpAdapter ? m_httpAdapter->port() : 0;
}

FamilyLinkHttpResponse FamilyLinkController::handleRequest(
    const FamilyLinkHttpRequest& request) const
{
    if (!m_bearerToken.isEmpty()) {
        const QByteArray expected = QByteArrayLiteral("Bearer ") + m_bearerToken;
        if (request.headers.value(QByteArrayLiteral("authorization")) != expected) {
            return errorResponse(401, QByteArrayLiteral("Unauthorized"),
                                 QStringLiteral("AUTHENTICATION_REQUIRED"),
                                 QStringLiteral("需要有效的家属端配对令牌"));
        }
    }

    const QUrl target = QUrl::fromEncoded(request.target);
    const QString path = target.path();
    if (path.isEmpty()) {
        return errorResponse(400, QByteArrayLiteral("Bad Request"),
                             QStringLiteral("BAD_REQUEST"),
                             QStringLiteral("请求地址无效"));
    }
    if (!isKnownReadPath(path)) {
        return errorResponse(404, QByteArrayLiteral("Not Found"),
                             QStringLiteral("ENDPOINT_NOT_FOUND"),
                             QStringLiteral("接口不存在"));
    }
    if (request.method != QByteArrayLiteral("GET")) {
        return errorResponse(405, QByteArrayLiteral("Method Not Allowed"),
                             QStringLiteral("READ_ONLY_API"),
                             QStringLiteral("当前 FamilyLink 版本仅开放只读接口"));
    }

    if (path == QStringLiteral("/api/v1/status"))
        return statusResponse();
    if (path == QStringLiteral("/api/v1/settings"))
        return settingsResponse();
    return remindersResponse();
}

FamilyLinkHttpResponse FamilyLinkController::statusResponse() const
{
    if (!m_service) {
        return errorResponse(503, QByteArrayLiteral("Service Unavailable"),
                             QStringLiteral("SERVICE_UNAVAILABLE"),
                             QStringLiteral("FamilyLink 服务尚未就绪"));
    }

    FamilyLinkStatusSnapshot snapshot;
    QString error;
    if (!m_service->status(&snapshot, &error)) {
        qWarning() << "FamilyLink status read failed:" << error;
        return errorResponse(500, QByteArrayLiteral("Internal Server Error"),
                             QStringLiteral("INTERNAL_ERROR"),
                             QStringLiteral("读取设备状态失败"));
    }

    const QJsonObject capabilities {
        {QStringLiteral("settingsRead"), true},
        {QStringLiteral("settingsWrite"), false},
        {QStringLiteral("remindersRead"), true},
        {QStringLiteral("remindersWrite"), false}
    };
    const QJsonObject device {
        {QStringLiteral("id"), snapshot.deviceId},
        {QStringLiteral("name"), snapshot.deviceName},
        {QStringLiteral("softwareVersion"), snapshot.device.softwareVersion},
        {QStringLiteral("online"), true},
        {QStringLiteral("lastSeenAt"), dateTimeValue(snapshot.observedAt)},
        {QStringLiteral("networkSummary"), snapshot.device.networkSummary},
        {QStringLiteral("powerSummary"), snapshot.device.powerSummary},
        {QStringLiteral("audioSummary"), snapshot.device.audioSummary},
        {QStringLiteral("brightnessSummary"), snapshot.device.brightnessSummary}
    };
    const QJsonObject system {
        {QStringLiteral("currentDateTime"), dateTimeValue(snapshot.system.currentDateTime)},
        {QStringLiteral("weatherSummary"), snapshot.system.weatherSummary},
        {QStringLiteral("networkKnown"), snapshot.system.networkKnown},
        {QStringLiteral("networkAvailable"), snapshot.system.networkAvailable},
        {QStringLiteral("batteryPercent"), snapshot.system.batteryPercent}
    };
    const QJsonObject care {
        {QStringLiteral("waterCompleted"), snapshot.care.waterCompleted},
        {QStringLiteral("waterGoal"), snapshot.care.waterGoal},
        {QStringLiteral("medicineCompleted"), snapshot.care.medicineCompleted},
        {QStringLiteral("medicineTotal"), snapshot.care.medicineTotal},
        {QStringLiteral("activityMinutes"), snapshot.care.activityMinutes},
        {QStringLiteral("interactionCount"), snapshot.care.interactionCount},
        {QStringLiteral("lastUpdated"), dateTimeValue(snapshot.care.lastUpdated)}
    };
    const QJsonObject root {
        {QStringLiteral("apiVersion"), QStringLiteral("1.0")},
        {QStringLiteral("capabilities"), capabilities},
        {QStringLiteral("device"), device},
        {QStringLiteral("system"), system},
        {QStringLiteral("care"), care}
    };
    return jsonResponse(200, QByteArrayLiteral("OK"), root);
}

FamilyLinkHttpResponse FamilyLinkController::settingsResponse() const
{
    if (!m_service) {
        return errorResponse(503, QByteArrayLiteral("Service Unavailable"),
                             QStringLiteral("SERVICE_UNAVAILABLE"),
                             QStringLiteral("FamilyLink 服务尚未就绪"));
    }

    FamilyLinkSettingsSnapshot snapshot;
    QString error;
    if (!m_service->settings(&snapshot, &error)) {
        qWarning() << "FamilyLink settings read failed:" << error;
        return errorResponse(500, QByteArrayLiteral("Internal Server Error"),
                             QStringLiteral("INTERNAL_ERROR"),
                             QStringLiteral("读取设备设置失败"));
    }

    const QJsonObject volumeCapability {
        {QStringLiteral("available"), snapshot.device.audioControlAvailable},
        {QStringLiteral("summary"), snapshot.device.audioSummary}
    };
    const QJsonObject brightnessCapability {
        {QStringLiteral("available"), snapshot.device.brightnessControlAvailable},
        {QStringLiteral("summary"), snapshot.device.brightnessSummary}
    };
    const QJsonObject capabilities {
        {QStringLiteral("volume"), volumeCapability},
        {QStringLiteral("brightness"), brightnessCapability}
    };
    const QJsonObject root {
        {QStringLiteral("volume"), snapshot.settings.volume},
        {QStringLiteral("brightness"), snapshot.settings.brightness},
        {QStringLiteral("petStyle"), snapshot.settings.petStyle},
        {QStringLiteral("revision"), snapshot.revision},
        {QStringLiteral("remoteWritable"), false},
        {QStringLiteral("capabilities"), capabilities}
    };
    return jsonResponse(200, QByteArrayLiteral("OK"), root);
}

FamilyLinkHttpResponse FamilyLinkController::remindersResponse() const
{
    if (!m_service) {
        return errorResponse(503, QByteArrayLiteral("Service Unavailable"),
                             QStringLiteral("SERVICE_UNAVAILABLE"),
                             QStringLiteral("FamilyLink 服务尚未就绪"));
    }

    QString error;
    const QList<Reminder> reminders = m_service->reminders(&error);
    if (!error.isEmpty()) {
        qWarning() << "FamilyLink reminders read failed:" << error;
        return errorResponse(500, QByteArrayLiteral("Internal Server Error"),
                             QStringLiteral("INTERNAL_ERROR"),
                             QStringLiteral("读取设备提醒失败"));
    }

    QJsonArray items;
    for (const Reminder& reminder : reminders) {
        items.append(QJsonObject {
            {QStringLiteral("id"), reminder.id},
            {QStringLiteral("type"), reminderTypeName(reminder.type)},
            {QStringLiteral("title"), reminder.title},
            {QStringLiteral("timeOfDay"), reminder.timeOfDay.toString(QStringLiteral("HH:mm"))},
            {QStringLiteral("scheduledDate"), reminder.scheduledDate.toString(Qt::ISODate)},
            {QStringLiteral("repeatRule"), repeatRuleName(reminder.repeatRule)},
            {QStringLiteral("enabled"), reminder.enabled},
            {QStringLiteral("revision"), reminder.revision},
            {QStringLiteral("status"), occurrenceStatusName(reminder.status)},
            {QStringLiteral("createdAt"), dateTimeValue(reminder.createdAt)},
            {QStringLiteral("updatedAt"), dateTimeValue(reminder.updatedAt)}
        });
    }
    return jsonResponse(200, QByteArrayLiteral("OK"),
                        QJsonObject {{QStringLiteral("items"), items},
                                     {QStringLiteral("remoteWritable"), false}});
}

FamilyLinkHttpResponse FamilyLinkController::jsonResponse(
    int statusCode, const QByteArray& reasonPhrase, const QJsonObject& object)
{
    return {statusCode, reasonPhrase,
            QJsonDocument(object).toJson(QJsonDocument::Compact)};
}

FamilyLinkHttpResponse FamilyLinkController::errorResponse(
    int statusCode, const QByteArray& reasonPhrase, const QString& code,
    const QString& message)
{
    const QJsonObject error {
        {QStringLiteral("code"), code},
        {QStringLiteral("message"), message}
    };
    return jsonResponse(statusCode, reasonPhrase,
                        QJsonObject {{QStringLiteral("error"), error}});
}
