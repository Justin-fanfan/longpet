#pragma once

#include "model/ReminderModels.h"
#include "platform/FamilyLinkHttpAdapter.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QUrl>

class FamilyLinkService;

class FamilyLinkController final {
public:
    FamilyLinkController(FamilyLinkService* service,
                         FamilyLinkHttpAdapter* httpAdapter,
                         QByteArray bearerToken = {});
    ~FamilyLinkController();

    bool start(quint16 port, QHostAddress address, QString* error = nullptr);
    void stop();
    quint16 port() const;

private:
    FamilyLinkHttpResponse handleRequest(const FamilyLinkHttpRequest& request) const;
    FamilyLinkHttpResponse statusResponse() const;
    FamilyLinkHttpResponse settingsResponse() const;
    FamilyLinkHttpResponse remindersResponse() const;
    FamilyLinkHttpResponse updateSettingsResponse(const QByteArray& body) const;
    FamilyLinkHttpResponse createReminderResponse(const QByteArray& body) const;
    FamilyLinkHttpResponse updateReminderResponse(ReminderId id,
                                                  const QByteArray& body) const;
    FamilyLinkHttpResponse deleteReminderResponse(ReminderId id,
                                                  const QUrl& target) const;
    static FamilyLinkHttpResponse jsonResponse(int statusCode,
                                               const QByteArray& reasonPhrase,
                                               const QJsonObject& object);
    static FamilyLinkHttpResponse errorResponse(int statusCode,
                                                const QByteArray& reasonPhrase,
                                                const QString& code,
                                                const QString& message,
                                                const QJsonObject& details = {});

    FamilyLinkService* m_service = nullptr;
    FamilyLinkHttpAdapter* m_httpAdapter = nullptr;
    QByteArray m_bearerToken;
};
