#include "NetworkStatusAdapter.h"

#include <QDebug>

#include <utility>

namespace {
QString transportText(QNetworkInformation::TransportMedium transportMedium)
{
    switch (transportMedium) {
    case QNetworkInformation::TransportMedium::Ethernet:
        return QStringLiteral("以太网");
    case QNetworkInformation::TransportMedium::Cellular:
        return QStringLiteral("蜂窝网络");
    case QNetworkInformation::TransportMedium::WiFi:
        return QStringLiteral("Wi-Fi");
    case QNetworkInformation::TransportMedium::Bluetooth:
        return QStringLiteral("蓝牙");
    case QNetworkInformation::TransportMedium::Unknown:
        return {};
    }
    return {};
}

QString withTransport(const QString& transport, const QString& state)
{
    return transport.isEmpty()
        ? state
        : QStringLiteral("%1 · %2").arg(transport, state);
}

bool sameStatus(const NetworkStatusSnapshot& left,
                const NetworkStatusSnapshot& right)
{
    return left.known == right.known
        && left.internetAvailable == right.internetAvailable
        && left.summary == right.summary;
}
}

NetworkStatusAdapter::NetworkStatusAdapter(QObject* parent)
    : NetworkStatusAdapter([] {
        if (QNetworkInformation* existing = QNetworkInformation::instance())
            return existing;
        if (!QNetworkInformation::loadDefaultBackend())
            return static_cast<QNetworkInformation*>(nullptr);
        return QNetworkInformation::instance();
    }, parent)
{
}

NetworkStatusAdapter::NetworkStatusAdapter(BackendProvider backendProvider,
                                           QObject* parent)
    : QObject(parent),
      m_backendProvider(std::move(backendProvider))
{
}

NetworkStatusAdapter::~NetworkStatusAdapter()
{
    stop();
}

bool NetworkStatusAdapter::start()
{
    if (m_networkInformation)
        return true;

    QNetworkInformation* information = m_backendProvider
        ? m_backendProvider() : nullptr;
    if (!information) {
        qWarning() << "QNetworkInformation backend unavailable; network status is unknown";
        publishStatus({false, false, QStringLiteral("网络状态未知")});
        return false;
    }

    m_networkInformation = information;
    m_backendName = information->backendName();
    qInfo() << "QNetworkInformation backend loaded:" << m_backendName;
    connect(information, &QNetworkInformation::reachabilityChanged,
            this, [this] { publishCurrentState(); });
    connect(information, &QNetworkInformation::transportMediumChanged,
            this, [this] { publishCurrentState(); });
    connect(information, &QNetworkInformation::isBehindCaptivePortalChanged,
            this, [this] { publishCurrentState(); });
    publishCurrentState();
    return true;
}

void NetworkStatusAdapter::stop()
{
    if (m_networkInformation)
        disconnect(m_networkInformation, nullptr, this, nullptr);
    m_networkInformation.clear();
    m_backendName.clear();
    m_lastStatus.reset();
}

bool NetworkStatusAdapter::isActive() const
{
    return !m_networkInformation.isNull();
}

QString NetworkStatusAdapter::backendName() const
{
    return m_backendName;
}

NetworkStatusSnapshot NetworkStatusAdapter::mapState(
    QNetworkInformation::Reachability reachability,
    QNetworkInformation::TransportMedium transportMedium,
    bool behindCaptivePortal)
{
    const QString transport = transportText(transportMedium);
    switch (reachability) {
    case QNetworkInformation::Reachability::Unknown:
        return {false, false, transport.isEmpty()
            ? QStringLiteral("网络状态未知")
            : withTransport(transport, QStringLiteral("状态未知"))};
    case QNetworkInformation::Reachability::Disconnected:
        return {true, false, QStringLiteral("未连接")};
    case QNetworkInformation::Reachability::Local:
    case QNetworkInformation::Reachability::Site:
        return {true, false, withTransport(transport, QStringLiteral("无互联网"))};
    case QNetworkInformation::Reachability::Online:
        if (behindCaptivePortal)
            return {true, false, withTransport(transport, QStringLiteral("需认证"))};
        return {true, true, withTransport(transport, QStringLiteral("已联网"))};
    }
    return {false, false, QStringLiteral("网络状态未知")};
}

void NetworkStatusAdapter::publishCurrentState()
{
    if (!m_networkInformation) {
        publishStatus({false, false, QStringLiteral("网络状态未知")});
        return;
    }

    const QNetworkInformation::Features features =
        m_networkInformation->supportedFeatures();
    const auto reachability = features.testFlag(QNetworkInformation::Feature::Reachability)
        ? m_networkInformation->reachability()
        : QNetworkInformation::Reachability::Unknown;
    const auto transport = features.testFlag(QNetworkInformation::Feature::TransportMedium)
        ? m_networkInformation->transportMedium()
        : QNetworkInformation::TransportMedium::Unknown;
    const bool captivePortal = features.testFlag(QNetworkInformation::Feature::CaptivePortal)
        && m_networkInformation->isBehindCaptivePortal();
    publishStatus(mapState(reachability, transport, captivePortal));
}

void NetworkStatusAdapter::publishStatus(const NetworkStatusSnapshot& status)
{
    if (m_lastStatus && sameStatus(*m_lastStatus, status))
        return;
    m_lastStatus = status;
    emit networkStateChanged(status.known, status.internetAvailable,
                             status.summary);
}
