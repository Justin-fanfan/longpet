#include "FamilyLinkController.h"

#include "services/FamilyLinkService.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>

#include <cmath>
#include <limits>
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

QString videoCallStateName(VideoCallState state)
{
    switch (state) {
    case VideoCallState::Idle: return QStringLiteral("idle");
    case VideoCallState::OutgoingRinging: return QStringLiteral("outgoing_ringing");
    case VideoCallState::NotifyingDevice: return QStringLiteral("notifying_device");
    case VideoCallState::ConnectingMedia: return QStringLiteral("connecting_media");
    case VideoCallState::Connected: return QStringLiteral("connected");
    case VideoCallState::Rejected: return QStringLiteral("rejected");
    case VideoCallState::Ended: return QStringLiteral("ended");
    case VideoCallState::Failed: return QStringLiteral("failed");
    }
    return QStringLiteral("idle");
}

QJsonObject videoCallObject(const VideoCallSnapshot& snapshot)
{
    return {
        {QStringLiteral("callId"), snapshot.callId},
        {QStringLiteral("state"), videoCallStateName(snapshot.state)},
        {QStringLiteral("mode"), snapshot.mode == VideoCallMode::Video
            ? QStringLiteral("video") : QStringLiteral("voice")},
        {QStringLiteral("direction"),
         snapshot.direction == VideoCallDirection::DeviceToFamily
            ? QStringLiteral("device_to_family") : QStringLiteral("family_to_device")},
        {QStringLiteral("remoteName"), snapshot.remoteName},
        {QStringLiteral("startedAt"), dateTimeValue(snapshot.startedAt)},
        {QStringLiteral("connectedAt"), dateTimeValue(snapshot.connectedAt)},
        {QStringLiteral("updatedAt"), dateTimeValue(snapshot.updatedAt)},
        {QStringLiteral("revision"), snapshot.revision},
        {QStringLiteral("mediaReady"), snapshot.mediaReady},
        {QStringLiteral("mediaProtocolVersion"), 1},
        {QStringLiteral("mediaPort"), snapshot.mediaPort},
        {QStringLiteral("mediaToken"), snapshot.mediaToken},
        {QStringLiteral("errorCode"), snapshot.errorCode.isEmpty()
             ? QJsonValue(QJsonValue::Null) : QJsonValue(snapshot.errorCode)},
        {QStringLiteral("errorMessage"), snapshot.errorMessage.isEmpty()
             ? QJsonValue(QJsonValue::Null) : QJsonValue(snapshot.errorMessage)}
    };
}

bool reminderTypeFromName(const QString& value, ReminderType* type)
{
    if (value == QStringLiteral("medicine")) {
        *type = ReminderType::Medicine;
        return true;
    }
    if (value == QStringLiteral("water")) {
        *type = ReminderType::Water;
        return true;
    }
    if (value == QStringLiteral("other")) {
        *type = ReminderType::Other;
        return true;
    }
    return false;
}

bool repeatRuleFromName(const QString& value, ReminderRepeatRule* rule)
{
    if (value == QStringLiteral("daily")) {
        *rule = ReminderRepeatRule::Daily;
        return true;
    }
    if (value == QStringLiteral("weekdays")) {
        *rule = ReminderRepeatRule::Weekdays;
        return true;
    }
    if (value == QStringLiteral("once")) {
        *rule = ReminderRepeatRule::Once;
        return true;
    }
    return false;
}

QJsonObject reminderObject(const Reminder& reminder)
{
    return {
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
    };
}

QJsonObject settingsObject(const FamilyLinkSettingsSnapshot& snapshot)
{
    const QJsonObject volumeCapability {
        {QStringLiteral("available"), snapshot.device.audioControlAvailable},
        {QStringLiteral("summary"), snapshot.device.audioSummary}
    };
    const QJsonObject brightnessCapability {
        {QStringLiteral("available"), snapshot.device.brightnessControlAvailable},
        {QStringLiteral("summary"), snapshot.device.brightnessSummary}
    };
    return {
        {QStringLiteral("volume"), snapshot.settings.volume},
        {QStringLiteral("brightness"), snapshot.settings.brightness},
        {QStringLiteral("petStyle"), snapshot.settings.petStyle},
        {QStringLiteral("revision"), snapshot.revision},
        {QStringLiteral("remoteWritable"), true},
        {QStringLiteral("capabilities"), QJsonObject {
            {QStringLiteral("volume"), volumeCapability},
            {QStringLiteral("brightness"), brightnessCapability}
        }}
    };
}

bool parseObjectBody(const QByteArray& body, QJsonObject* object, QString* error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = QStringLiteral("请求正文必须是 JSON 对象");
        return false;
    }
    *object = document.object();
    return true;
}

bool hasOnlyKeys(const QJsonObject& object, const QSet<QString>& allowed,
                 QString* error)
{
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!allowed.contains(it.key())) {
            if (error)
                *error = QStringLiteral("不支持的字段：%1").arg(it.key());
            return false;
        }
    }
    return true;
}

bool integerField(const QJsonObject& object, const QString& key,
                  int minimum, int maximum, int* value, QString* error)
{
    const QJsonValue field = object.value(key);
    if (!field.isDouble()) {
        if (error)
            *error = QStringLiteral("%1 必须是整数").arg(key);
        return false;
    }
    const double number = field.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number
        || number < minimum || number > maximum) {
        if (error)
            *error = QStringLiteral("%1 超出允许范围").arg(key);
        return false;
    }
    *value = static_cast<int>(number);
    return true;
}

bool parseSettingsUpdate(const QByteArray& body, SettingsUpdateRequest* request,
                         QString* error)
{
    QJsonObject object;
    if (!parseObjectBody(body, &object, error))
        return false;
    const QSet<QString> allowed {
        QStringLiteral("volume"), QStringLiteral("brightness"),
        QStringLiteral("petStyle"), QStringLiteral("expectedRevision")
    };
    if (!hasOnlyKeys(object, allowed, error))
        return false;
    if (!integerField(object, QStringLiteral("expectedRevision"), 0,
                      std::numeric_limits<int>::max(), &request->expectedRevision, error)) {
        return false;
    }
    if (object.contains(QStringLiteral("volume"))) {
        int value = 0;
        if (!integerField(object, QStringLiteral("volume"), 0, 100, &value, error))
            return false;
        request->volume = value;
    }
    if (object.contains(QStringLiteral("brightness"))) {
        int value = 0;
        if (!integerField(object, QStringLiteral("brightness"), 0, 100, &value, error))
            return false;
        request->brightness = value;
    }
    if (object.contains(QStringLiteral("petStyle"))) {
        if (!object.value(QStringLiteral("petStyle")).isString()) {
            if (error)
                *error = QStringLiteral("petStyle 必须是字符串");
            return false;
        }
        request->petStyle = object.value(QStringLiteral("petStyle")).toString();
    }
    if (request->isEmpty()) {
        if (error)
            *error = QStringLiteral("没有可保存的设置字段");
        return false;
    }
    return true;
}

bool parseReminderDraft(const QByteArray& body, bool updating,
                        ReminderDraft* draft, QString* error)
{
    QJsonObject object;
    if (!parseObjectBody(body, &object, error))
        return false;
    QSet<QString> allowed {
        QStringLiteral("type"), QStringLiteral("title"),
        QStringLiteral("timeOfDay"), QStringLiteral("scheduledDate"),
        QStringLiteral("repeatRule"), QStringLiteral("enabled")
    };
    if (updating)
        allowed.insert(QStringLiteral("expectedRevision"));
    if (!hasOnlyKeys(object, allowed, error))
        return false;

    const QStringList stringFields {
        QStringLiteral("type"), QStringLiteral("title"), QStringLiteral("timeOfDay"),
        QStringLiteral("scheduledDate"), QStringLiteral("repeatRule")
    };
    for (const QString& field : stringFields) {
        if (!object.value(field).isString()) {
            if (error)
                *error = QStringLiteral("%1 必须是字符串").arg(field);
            return false;
        }
    }
    if (!object.value(QStringLiteral("enabled")).isBool()) {
        if (error)
            *error = QStringLiteral("enabled 必须是布尔值");
        return false;
    }

    draft->title = object.value(QStringLiteral("title")).toString().simplified();
    if (draft->title.isEmpty() || draft->title.size() > 40) {
        if (error)
            *error = QStringLiteral("提醒标题长度必须为 1 到 40 个字符");
        return false;
    }
    if (!reminderTypeFromName(object.value(QStringLiteral("type")).toString(),
                              &draft->type)) {
        if (error)
            *error = QStringLiteral("提醒类型无效");
        return false;
    }
    if (!repeatRuleFromName(object.value(QStringLiteral("repeatRule")).toString(),
                            &draft->repeatRule)) {
        if (error)
            *error = QStringLiteral("重复规则无效");
        return false;
    }
    const QString timeText = object.value(QStringLiteral("timeOfDay")).toString();
    draft->timeOfDay = QTime::fromString(timeText, QStringLiteral("HH:mm"));
    if (!draft->timeOfDay.isValid()
        || draft->timeOfDay.toString(QStringLiteral("HH:mm")) != timeText) {
        if (error)
            *error = QStringLiteral("提醒时间必须使用 HH:mm 格式");
        return false;
    }
    const QString dateText = object.value(QStringLiteral("scheduledDate")).toString();
    draft->scheduledDate = QDate::fromString(dateText, Qt::ISODate);
    if (!draft->scheduledDate.isValid()
        || draft->scheduledDate.toString(Qt::ISODate) != dateText) {
        if (error)
            *error = QStringLiteral("提醒日期必须使用 YYYY-MM-DD 格式");
        return false;
    }
    draft->enabled = object.value(QStringLiteral("enabled")).toBool();
    if (updating
        && !integerField(object, QStringLiteral("expectedRevision"), 0,
                         std::numeric_limits<int>::max(), &draft->expectedRevision, error)) {
        return false;
    }
    return true;
}

bool reminderIdFromPath(const QString& path, ReminderId* id)
{
    const QString prefix = QStringLiteral("/api/v1/reminders/");
    if (!path.startsWith(prefix))
        return false;
    const QString idText = path.mid(prefix.size());
    bool valid = false;
    const qlonglong parsed = idText.toLongLong(&valid);
    if (!valid || parsed <= 0)
        return false;
    *id = parsed;
    return true;
}

bool parseVideoCallAction(const QByteArray& body, VideoCallActionRequest* request,
                          QString* error)
{
    QJsonObject object;
    if (!parseObjectBody(body, &object, error))
        return false;
    const QSet<QString> allowed {
        QStringLiteral("callId"), QStringLiteral("action"),
        QStringLiteral("expectedRevision"), QStringLiteral("errorCode"),
        QStringLiteral("errorMessage")
    };
    if (!hasOnlyKeys(object, allowed, error))
        return false;
    if (!object.value(QStringLiteral("callId")).isString()
        || object.value(QStringLiteral("callId")).toString().trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("callId 必须是非空字符串");
        return false;
    }
    if (!object.value(QStringLiteral("action")).isString()) {
        if (error)
            *error = QStringLiteral("action 必须是字符串");
        return false;
    }
    if (!integerField(object, QStringLiteral("expectedRevision"), 0,
                      std::numeric_limits<int>::max(),
                      &request->expectedRevision, error)) {
        return false;
    }
    request->callId = object.value(QStringLiteral("callId")).toString().trimmed();
    const QString action = object.value(QStringLiteral("action")).toString();
    if (action == QStringLiteral("accept"))
        request->action = VideoCallAction::Accept;
    else if (action == QStringLiteral("reject"))
        request->action = VideoCallAction::Reject;
    else if (action == QStringLiteral("hangup"))
        request->action = VideoCallAction::HangUp;
    else if (action == QStringLiteral("fail"))
        request->action = VideoCallAction::Fail;
    else {
        if (error)
            *error = QStringLiteral("action 只支持 accept、reject、hangup 或 fail");
        return false;
    }
    if (object.contains(QStringLiteral("errorCode"))) {
        if (!object.value(QStringLiteral("errorCode")).isString()) {
            if (error)
                *error = QStringLiteral("errorCode 必须是字符串");
            return false;
        }
        request->errorCode = object.value(QStringLiteral("errorCode")).toString().left(80);
    }
    if (object.contains(QStringLiteral("errorMessage"))) {
        if (!object.value(QStringLiteral("errorMessage")).isString()) {
            if (error)
                *error = QStringLiteral("errorMessage 必须是字符串");
            return false;
        }
        request->errorMessage = object.value(QStringLiteral("errorMessage"))
                                    .toString().left(300);
    }
    return true;
}

bool parseVideoCallStart(const QByteArray& body, VideoCallMode* mode,
                         QString* error)
{
    QJsonObject object;
    if (!parseObjectBody(body, &object, error))
        return false;
    if (!hasOnlyKeys(object, {QStringLiteral("mode")}, error)
        || !object.value(QStringLiteral("mode")).isString()) {
        if (error && error->isEmpty())
            *error = QStringLiteral("mode 必须是字符串");
        return false;
    }
    const QString value = object.value(QStringLiteral("mode")).toString();
    if (value == QStringLiteral("voice")) {
        *mode = VideoCallMode::Voice;
        return true;
    }
    if (value == QStringLiteral("video")) {
        *mode = VideoCallMode::Video;
        return true;
    }
    if (error)
        *error = QStringLiteral("mode 只支持 voice 或 video");
    return false;
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
    if (path == QStringLiteral("/api/v1/status")) {
        if (request.method == QByteArrayLiteral("GET"))
            return statusResponse();
    } else if (path == QStringLiteral("/api/v1/settings")) {
        if (request.method == QByteArrayLiteral("GET"))
            return settingsResponse();
        if (request.method == QByteArrayLiteral("PATCH"))
            return updateSettingsResponse(request.body);
    } else if (path == QStringLiteral("/api/v1/reminders")) {
        if (request.method == QByteArrayLiteral("GET"))
            return remindersResponse();
        if (request.method == QByteArrayLiteral("POST"))
            return createReminderResponse(request.body);
    } else if (path == QStringLiteral("/api/v1/video-call")) {
        if (request.method == QByteArrayLiteral("GET"))
            return videoCallResponse();
        if (request.method == QByteArrayLiteral("POST"))
            return startVideoCallResponse(request.body);
    } else if (path == QStringLiteral("/api/v1/video-call/actions")) {
        if (request.method == QByteArrayLiteral("POST"))
            return videoCallActionResponse(request.body);
    } else {
        ReminderId id = 0;
        if (!reminderIdFromPath(path, &id)) {
            return errorResponse(404, QByteArrayLiteral("Not Found"),
                                 QStringLiteral("ENDPOINT_NOT_FOUND"),
                                 QStringLiteral("接口不存在"));
        }
        if (request.method == QByteArrayLiteral("PUT"))
            return updateReminderResponse(id, request.body);
        if (request.method == QByteArrayLiteral("DELETE"))
            return deleteReminderResponse(id, target);
    }

    return errorResponse(405, QByteArrayLiteral("Method Not Allowed"),
                         QStringLiteral("METHOD_NOT_ALLOWED"),
                         QStringLiteral("该接口不支持当前请求方法"));
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
        {QStringLiteral("settingsWrite"), true},
        {QStringLiteral("remindersRead"), true},
        {QStringLiteral("remindersWrite"), true},
        {QStringLiteral("videoCallSignaling"), m_service->videoCallAvailable()}
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

    return jsonResponse(200, QByteArrayLiteral("OK"), settingsObject(snapshot));
}

FamilyLinkHttpResponse FamilyLinkController::videoCallResponse() const
{
    if (!m_service || !m_service->videoCallAvailable()) {
        return errorResponse(503, QByteArrayLiteral("Service Unavailable"),
                             QStringLiteral("SERVICE_UNAVAILABLE"),
                             QStringLiteral("视频通话服务尚未就绪"));
    }
    VideoCallSnapshot snapshot;
    QString error;
    if (!m_service->videoCall(&snapshot, &error)) {
        qWarning() << "FamilyLink video call read failed:" << error;
        return errorResponse(500, QByteArrayLiteral("Internal Server Error"),
                             QStringLiteral("INTERNAL_ERROR"),
                             QStringLiteral("读取视频通话状态失败"));
    }
    return jsonResponse(200, QByteArrayLiteral("OK"), videoCallObject(snapshot));
}

FamilyLinkHttpResponse FamilyLinkController::videoCallActionResponse(
    const QByteArray& body) const
{
    if (!m_service || !m_service->videoCallAvailable()) {
        return errorResponse(503, QByteArrayLiteral("Service Unavailable"),
                             QStringLiteral("SERVICE_UNAVAILABLE"),
                             QStringLiteral("视频通话服务尚未就绪"));
    }
    VideoCallActionRequest request;
    QString validationError;
    if (!parseVideoCallAction(body, &request, &validationError)) {
        return errorResponse(422, QByteArrayLiteral("Unprocessable Content"),
                             QStringLiteral("VALIDATION_ERROR"), validationError);
    }
    const VideoCallResult result = m_service->applyVideoCallAction(request);
    if (!result.success) {
        const QString code = result.code == VideoCallErrorCode::RevisionConflict
            ? QStringLiteral("REVISION_CONFLICT")
            : result.code == VideoCallErrorCode::CallMismatch
            ? QStringLiteral("CALL_MISMATCH")
            : QStringLiteral("INVALID_CALL_STATE");
        return errorResponse(409, QByteArrayLiteral("Conflict"), code, result.error,
                             QJsonObject {{QStringLiteral("currentRevision"),
                                           result.snapshot.revision},
                                          {QStringLiteral("currentState"),
                                           videoCallStateName(result.snapshot.state)}});
    }
    return jsonResponse(200, QByteArrayLiteral("OK"),
                        videoCallObject(result.snapshot));
}

FamilyLinkHttpResponse FamilyLinkController::startVideoCallResponse(
    const QByteArray& body) const
{
    if (!m_service || !m_service->videoCallAvailable()) {
        return errorResponse(503, QByteArrayLiteral("Service Unavailable"),
                             QStringLiteral("SERVICE_UNAVAILABLE"),
                             QStringLiteral("视频通话服务尚未就绪"));
    }
    VideoCallMode mode = VideoCallMode::Video;
    QString validationError;
    if (!parseVideoCallStart(body, &mode, &validationError)) {
        return errorResponse(422, QByteArrayLiteral("Unprocessable Content"),
                             QStringLiteral("VALIDATION_ERROR"), validationError);
    }
    const VideoCallResult result = m_service->startVideoCall(mode);
    if (!result.success) {
        if (result.code == VideoCallErrorCode::Busy) {
            return errorResponse(409, QByteArrayLiteral("Conflict"),
                                 QStringLiteral("DEVICE_BUSY"), result.error,
                                 {{QStringLiteral("currentState"),
                                   videoCallStateName(result.snapshot.state)}});
        }
        return errorResponse(503, QByteArrayLiteral("Service Unavailable"),
                             QStringLiteral("MEDIA_INITIALIZATION_FAILED"),
                             result.error, {{QStringLiteral("call"),
                                             videoCallObject(result.snapshot)}});
    }
    return jsonResponse(201, QByteArrayLiteral("Created"),
                        videoCallObject(result.snapshot));
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
    for (const Reminder& reminder : reminders)
        items.append(reminderObject(reminder));
    return jsonResponse(200, QByteArrayLiteral("OK"),
                        QJsonObject {{QStringLiteral("items"), items},
                                     {QStringLiteral("remoteWritable"), true}});
}

FamilyLinkHttpResponse FamilyLinkController::updateSettingsResponse(
    const QByteArray& body) const
{
    if (!m_service) {
        return errorResponse(503, QByteArrayLiteral("Service Unavailable"),
                             QStringLiteral("SERVICE_UNAVAILABLE"),
                             QStringLiteral("FamilyLink 服务尚未就绪"));
    }
    SettingsUpdateRequest request;
    QString validationError;
    if (!parseSettingsUpdate(body, &request, &validationError)) {
        return errorResponse(422, QByteArrayLiteral("Unprocessable Content"),
                             QStringLiteral("VALIDATION_ERROR"), validationError);
    }

    const SettingsUpdateResult result = m_service->updateSettings(request);
    if (!result.success) {
        if (result.code == SettingsUpdateErrorCode::RevisionConflict) {
            return errorResponse(409, QByteArrayLiteral("Conflict"),
                                 QStringLiteral("REVISION_CONFLICT"), result.error,
                                 QJsonObject {{QStringLiteral("currentRevision"),
                                               result.revision}});
        }
        if (result.code == SettingsUpdateErrorCode::Validation) {
            return errorResponse(422, QByteArrayLiteral("Unprocessable Content"),
                                 QStringLiteral("VALIDATION_ERROR"), result.error);
        }
        if (result.code == SettingsUpdateErrorCode::CapabilityUnavailable) {
            return errorResponse(503, QByteArrayLiteral("Service Unavailable"),
                                 QStringLiteral("CAPABILITY_UNAVAILABLE"), result.error);
        }
        qWarning() << "FamilyLink settings update failed:" << result.error;
        return errorResponse(500, QByteArrayLiteral("Internal Server Error"),
                             QStringLiteral("INTERNAL_ERROR"),
                             QStringLiteral("保存设备设置失败"));
    }

    FamilyLinkSettingsSnapshot snapshot;
    QString error;
    if (!m_service->settings(&snapshot, &error)) {
        qWarning() << "FamilyLink updated settings read failed:" << error;
        return errorResponse(500, QByteArrayLiteral("Internal Server Error"),
                             QStringLiteral("INTERNAL_ERROR"),
                             QStringLiteral("读取更新后的设置失败"));
    }
    return jsonResponse(200, QByteArrayLiteral("OK"), settingsObject(snapshot));
}

FamilyLinkHttpResponse FamilyLinkController::createReminderResponse(
    const QByteArray& body) const
{
    if (!m_service) {
        return errorResponse(503, QByteArrayLiteral("Service Unavailable"),
                             QStringLiteral("SERVICE_UNAVAILABLE"),
                             QStringLiteral("FamilyLink 服务尚未就绪"));
    }
    ReminderDraft draft;
    QString validationError;
    if (!parseReminderDraft(body, false, &draft, &validationError)) {
        return errorResponse(422, QByteArrayLiteral("Unprocessable Content"),
                             QStringLiteral("VALIDATION_ERROR"), validationError);
    }

    Reminder saved;
    const ServiceResult result = m_service->saveReminder(draft, &saved);
    if (!result.success) {
        if (result.code == ServiceErrorCode::Validation) {
            return errorResponse(422, QByteArrayLiteral("Unprocessable Content"),
                                 QStringLiteral("VALIDATION_ERROR"), result.error);
        }
        qWarning() << "FamilyLink reminder create failed:" << result.error;
        return errorResponse(500, QByteArrayLiteral("Internal Server Error"),
                             QStringLiteral("INTERNAL_ERROR"),
                             QStringLiteral("创建提醒失败"));
    }
    return jsonResponse(201, QByteArrayLiteral("Created"), reminderObject(saved));
}

FamilyLinkHttpResponse FamilyLinkController::updateReminderResponse(
    ReminderId id, const QByteArray& body) const
{
    if (!m_service) {
        return errorResponse(503, QByteArrayLiteral("Service Unavailable"),
                             QStringLiteral("SERVICE_UNAVAILABLE"),
                             QStringLiteral("FamilyLink 服务尚未就绪"));
    }
    ReminderDraft draft;
    QString validationError;
    if (!parseReminderDraft(body, true, &draft, &validationError)) {
        return errorResponse(422, QByteArrayLiteral("Unprocessable Content"),
                             QStringLiteral("VALIDATION_ERROR"), validationError);
    }
    draft.id = id;

    Reminder saved;
    const ServiceResult result = m_service->saveReminder(draft, &saved);
    if (!result.success) {
        if (result.code == ServiceErrorCode::RevisionConflict) {
            return errorResponse(409, QByteArrayLiteral("Conflict"),
                                 QStringLiteral("REVISION_CONFLICT"), result.error,
                                 QJsonObject {{QStringLiteral("currentRevision"),
                                               result.currentRevision}});
        }
        if (result.code == ServiceErrorCode::NotFound) {
            return errorResponse(404, QByteArrayLiteral("Not Found"),
                                 QStringLiteral("REMINDER_NOT_FOUND"), result.error);
        }
        if (result.code == ServiceErrorCode::Validation) {
            return errorResponse(422, QByteArrayLiteral("Unprocessable Content"),
                                 QStringLiteral("VALIDATION_ERROR"), result.error);
        }
        qWarning() << "FamilyLink reminder update failed:" << result.error;
        return errorResponse(500, QByteArrayLiteral("Internal Server Error"),
                             QStringLiteral("INTERNAL_ERROR"),
                             QStringLiteral("更新提醒失败"));
    }
    return jsonResponse(200, QByteArrayLiteral("OK"), reminderObject(saved));
}

FamilyLinkHttpResponse FamilyLinkController::deleteReminderResponse(
    ReminderId id, const QUrl& target) const
{
    if (!m_service) {
        return errorResponse(503, QByteArrayLiteral("Service Unavailable"),
                             QStringLiteral("SERVICE_UNAVAILABLE"),
                             QStringLiteral("FamilyLink 服务尚未就绪"));
    }
    bool valid = false;
    const int expectedRevision = QUrlQuery(target).queryItemValue(
        QStringLiteral("expectedRevision")).toInt(&valid);
    if (!valid || expectedRevision < 0) {
        return errorResponse(422, QByteArrayLiteral("Unprocessable Content"),
                             QStringLiteral("VALIDATION_ERROR"),
                             QStringLiteral("expectedRevision 必须是非负整数"));
    }

    const ServiceResult result = m_service->removeReminder(id, expectedRevision);
    if (!result.success) {
        if (result.code == ServiceErrorCode::RevisionConflict) {
            return errorResponse(409, QByteArrayLiteral("Conflict"),
                                 QStringLiteral("REVISION_CONFLICT"), result.error,
                                 QJsonObject {{QStringLiteral("currentRevision"),
                                               result.currentRevision}});
        }
        if (result.code == ServiceErrorCode::NotFound) {
            return errorResponse(404, QByteArrayLiteral("Not Found"),
                                 QStringLiteral("REMINDER_NOT_FOUND"), result.error);
        }
        qWarning() << "FamilyLink reminder delete failed:" << result.error;
        return errorResponse(500, QByteArrayLiteral("Internal Server Error"),
                             QStringLiteral("INTERNAL_ERROR"),
                             QStringLiteral("删除提醒失败"));
    }
    return jsonResponse(200, QByteArrayLiteral("OK"),
                        QJsonObject {{QStringLiteral("deleted"), true},
                                     {QStringLiteral("id"), id}});
}

FamilyLinkHttpResponse FamilyLinkController::jsonResponse(
    int statusCode, const QByteArray& reasonPhrase, const QJsonObject& object)
{
    return {statusCode, reasonPhrase,
            QJsonDocument(object).toJson(QJsonDocument::Compact)};
}

FamilyLinkHttpResponse FamilyLinkController::errorResponse(
    int statusCode, const QByteArray& reasonPhrase, const QString& code,
    const QString& message, const QJsonObject& details)
{
    QJsonObject error {
        {QStringLiteral("code"), code},
        {QStringLiteral("message"), message}
    };
    if (!details.isEmpty())
        error.insert(QStringLiteral("details"), details);
    return jsonResponse(statusCode, reasonPhrase,
                        QJsonObject {{QStringLiteral("error"), error}});
}
