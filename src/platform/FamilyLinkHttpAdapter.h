#pragma once

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QTcpServer>

#include <functional>

class QTcpSocket;

struct FamilyLinkHttpRequest {
    QByteArray method;
    QByteArray target;
    QHash<QByteArray, QByteArray> headers;
    QByteArray body;
};

struct FamilyLinkHttpResponse {
    int statusCode = 200;
    QByteArray reasonPhrase = QByteArrayLiteral("OK");
    QByteArray body;
};

class FamilyLinkHttpAdapter final : public QObject {
public:
    using RequestHandler = std::function<FamilyLinkHttpResponse(
        const FamilyLinkHttpRequest& request)>;

    explicit FamilyLinkHttpAdapter(QObject* parent = nullptr);
    ~FamilyLinkHttpAdapter() override;

    void setRequestHandler(RequestHandler handler);
    bool start(quint16 port, QString* error = nullptr,
               QHostAddress address = QHostAddress(QHostAddress::LocalHost));
    void stop();
    bool isListening() const;
    quint16 port() const;

private:
    void acceptPendingConnections();
    void readRequest(QTcpSocket* socket);
    void writeResponse(QTcpSocket* socket, const FamilyLinkHttpResponse& response);
    static FamilyLinkHttpResponse transportError(int statusCode,
                                                 const QByteArray& reasonPhrase,
                                                 const QByteArray& code,
                                                 const QByteArray& message);

    static constexpr qsizetype MaximumHeaderBytes = 16 * 1024;
    static constexpr qsizetype MaximumBodyBytes = 64 * 1024;

    QTcpServer m_server;
    QHash<QTcpSocket*, QByteArray> m_requestBuffers;
    RequestHandler m_handler;
};
