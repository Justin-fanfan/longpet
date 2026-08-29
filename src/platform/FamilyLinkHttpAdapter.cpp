#include "FamilyLinkHttpAdapter.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>

#include <utility>

FamilyLinkHttpAdapter::FamilyLinkHttpAdapter(QObject* parent)
    : QObject(parent),
      m_server(this)
{
    m_server.setMaxPendingConnections(8);
    connect(&m_server, &QTcpServer::newConnection,
            this, &FamilyLinkHttpAdapter::acceptPendingConnections);
}

FamilyLinkHttpAdapter::~FamilyLinkHttpAdapter()
{
    stop();
}

void FamilyLinkHttpAdapter::setRequestHandler(RequestHandler handler)
{
    m_handler = std::move(handler);
}

bool FamilyLinkHttpAdapter::start(quint16 port, QString* error, QHostAddress address)
{
    if (m_server.isListening())
        return true;
    if (m_server.listen(address, port))
        return true;
    if (error)
        *error = m_server.errorString();
    return false;
}

void FamilyLinkHttpAdapter::stop()
{
    m_server.close();
    const QList<QTcpSocket*> sockets = m_requestBuffers.keys();
    m_requestBuffers.clear();
    for (QTcpSocket* socket : sockets) {
        if (!socket)
            continue;
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }
}

bool FamilyLinkHttpAdapter::isListening() const
{
    return m_server.isListening();
}

quint16 FamilyLinkHttpAdapter::port() const
{
    return m_server.serverPort();
}

void FamilyLinkHttpAdapter::acceptPendingConnections()
{
    while (m_server.hasPendingConnections()) {
        QTcpSocket* socket = m_server.nextPendingConnection();
        if (!socket)
            continue;
        m_requestBuffers.insert(socket, {});
        connect(socket, &QTcpSocket::readyRead, this,
                [this, socket] { readRequest(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            m_requestBuffers.remove(socket);
            socket->deleteLater();
        });
    }
}

void FamilyLinkHttpAdapter::readRequest(QTcpSocket* socket)
{
    if (!socket || !m_requestBuffers.contains(socket))
        return;

    QByteArray& buffer = m_requestBuffers[socket];
    buffer.append(socket->readAll());
    if (buffer.size() > MaximumHeaderBytes) {
        m_requestBuffers.remove(socket);
        writeResponse(socket, transportError(431, QByteArrayLiteral("Request Header Fields Too Large"),
                                             QByteArrayLiteral("REQUEST_TOO_LARGE"),
                                             QByteArrayLiteral("Request headers are too large")));
        return;
    }

    const qsizetype headerEnd = buffer.indexOf(QByteArrayLiteral("\r\n\r\n"));
    if (headerEnd < 0)
        return;

    const QByteArray headerBlock = buffer.left(headerEnd);
    m_requestBuffers.remove(socket);
    QList<QByteArray> lines = headerBlock.split('\n');
    if (lines.isEmpty()) {
        writeResponse(socket, transportError(400, QByteArrayLiteral("Bad Request"),
                                             QByteArrayLiteral("BAD_REQUEST"),
                                             QByteArrayLiteral("Invalid HTTP request")));
        return;
    }

    const QList<QByteArray> requestLine = lines.takeFirst().trimmed().split(' ');
    if (requestLine.size() != 3 || !requestLine.at(2).startsWith(QByteArrayLiteral("HTTP/1."))) {
        writeResponse(socket, transportError(400, QByteArrayLiteral("Bad Request"),
                                             QByteArrayLiteral("BAD_REQUEST"),
                                             QByteArrayLiteral("Invalid HTTP request line")));
        return;
    }

    FamilyLinkHttpRequest request;
    request.method = requestLine.at(0).trimmed().toUpper();
    request.target = requestLine.at(1).trimmed();
    for (const QByteArray& rawLine : std::as_const(lines)) {
        const QByteArray line = rawLine.trimmed();
        if (line.isEmpty())
            continue;
        const qsizetype separator = line.indexOf(':');
        if (separator <= 0) {
            writeResponse(socket, transportError(400, QByteArrayLiteral("Bad Request"),
                                                 QByteArrayLiteral("BAD_REQUEST"),
                                                 QByteArrayLiteral("Invalid HTTP header")));
            return;
        }
        request.headers.insert(line.left(separator).trimmed().toLower(),
                               line.mid(separator + 1).trimmed());
    }

    const FamilyLinkHttpResponse response = m_handler
        ? m_handler(request)
        : transportError(503, QByteArrayLiteral("Service Unavailable"),
                         QByteArrayLiteral("SERVICE_UNAVAILABLE"),
                         QByteArrayLiteral("FamilyLink handler is unavailable"));
    writeResponse(socket, response);
}

void FamilyLinkHttpAdapter::writeResponse(QTcpSocket* socket,
                                          const FamilyLinkHttpResponse& response)
{
    if (!socket)
        return;
    QByteArray payload;
    payload.reserve(response.body.size() + 256);
    payload.append("HTTP/1.1 ");
    payload.append(QByteArray::number(response.statusCode));
    payload.append(' ');
    payload.append(response.reasonPhrase);
    payload.append("\r\nContent-Type: application/json; charset=utf-8\r\n");
    payload.append("Cache-Control: no-store\r\nConnection: close\r\nContent-Length: ");
    payload.append(QByteArray::number(response.body.size()));
    payload.append("\r\n\r\n");
    payload.append(response.body);
    socket->write(payload);
    socket->disconnectFromHost();
}

FamilyLinkHttpResponse FamilyLinkHttpAdapter::transportError(
    int statusCode, const QByteArray& reasonPhrase, const QByteArray& code,
    const QByteArray& message)
{
    const QJsonObject error {
        {QStringLiteral("code"), QString::fromUtf8(code)},
        {QStringLiteral("message"), QString::fromUtf8(message)}
    };
    const QJsonObject root {{QStringLiteral("error"), error}};
    return {statusCode, reasonPhrase,
            QJsonDocument(root).toJson(QJsonDocument::Compact)};
}
