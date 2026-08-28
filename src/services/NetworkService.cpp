#include "NetworkService.h"

#include "platform/NetworkManagerAdapter.h"

NetworkService::NetworkService(NetworkManagerAdapter* adapter, QObject* parent)
    : QObject(parent),
      m_adapter(adapter)
{
    if (!m_adapter)
        return;
    connect(m_adapter, &NetworkManagerAdapter::scanStarted,
            this, &NetworkService::scanStarted);
    connect(m_adapter, &NetworkManagerAdapter::scanCompleted,
            this, &NetworkService::networksChanged);
    connect(m_adapter, &NetworkManagerAdapter::scanFailed,
            this, &NetworkService::scanFailed);
    connect(m_adapter, &NetworkManagerAdapter::connectionStarted,
            this, &NetworkService::connectionStarted);
    connect(m_adapter, &NetworkManagerAdapter::connectionSucceeded,
            this, &NetworkService::connectionSucceeded);
    connect(m_adapter, &NetworkManagerAdapter::connectionFailed,
            this, &NetworkService::connectionFailed);
}

bool NetworkService::isBusy() const
{
    return m_adapter && m_adapter->isBusy();
}

void NetworkService::scanNetworks()
{
    if (!m_adapter) {
        emit scanFailed(QStringLiteral("网络配置后端不可用"));
        return;
    }
    m_adapter->scanNetworks();
}

void NetworkService::connectToNetwork(const WifiNetwork& network,
                                      const QString& password)
{
    if (!m_adapter) {
        emit connectionFailed(network.ssid, QStringLiteral("网络配置后端不可用"));
        return;
    }
    if (network.ssid.trimmed().isEmpty()) {
        emit connectionFailed(network.ssid, QStringLiteral("请选择要连接的网络"));
        return;
    }
    if (!network.supported) {
        emit connectionFailed(network.ssid,
                              QStringLiteral("暂不支持企业级 802.1X 网络"));
        return;
    }
    if (network.requiresPassword && password.isEmpty()) {
        emit connectionFailed(network.ssid, QStringLiteral("请输入网络密码"));
        return;
    }
    if (password.contains(QLatin1Char('\n')) || password.contains(QLatin1Char('\r'))) {
        emit connectionFailed(network.ssid, QStringLiteral("网络密码格式无效"));
        return;
    }
    m_adapter->connectToNetwork(network, password);
}
