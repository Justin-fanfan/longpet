#include "NetworkSetupPage.h"

#include "widgets/VisualComponents.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
QString securityText(const WifiNetwork& network)
{
    if (network.security.isEmpty())
        return QStringLiteral("开放网络");
    if (!network.supported)
        return QStringLiteral("%1 · 暂不支持").arg(network.security);
    return network.security;
}

QString networkItemText(const WifiNetwork& network)
{
    const QString connected = network.connected
        ? QStringLiteral(" · 已连接") : QString();
    return QStringLiteral("%1\n信号 %2% · %3%4")
        .arg(network.ssid)
        .arg(network.signalStrength)
        .arg(securityText(network), connected);
}
}

NetworkSetupPage::NetworkSetupPage(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("networkSetupPage"));
    setProperty("page", true);
    setAttribute(Qt::WA_StyledBackground, true);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto* header = new PageHeaderWidget(QStringLiteral("网络连接"), this);
    root->addWidget(header);
    connect(header, &PageHeaderWidget::backRequested, this, [this] {
        clearPassword();
        emit backRequested();
    });

    auto* content = new QHBoxLayout;
    content->setContentsMargins(32, 12, 32, 24);
    content->setSpacing(24);

    auto* networksCard = new SectionCard(this);
    networksCard->setMinimumWidth(520);
    networksCard->contentLayout()->addWidget(
        makeLabel(QStringLiteral("可用 Wi-Fi"), "body", networksCard));
    m_scanStatus = makeLabel(QStringLiteral("点击刷新开始扫描"), "assist", networksCard);
    m_scanStatus->setObjectName(QStringLiteral("wifiScanStatus"));
    networksCard->contentLayout()->addWidget(m_scanStatus);
    m_networkList = new QListWidget(networksCard);
    m_networkList->setObjectName(QStringLiteral("wifiNetworkList"));
    m_networkList->setSpacing(4);
    networksCard->contentLayout()->addWidget(m_networkList, 1);
    m_refreshButton = new QPushButton(QStringLiteral("刷新网络"), networksCard);
    m_refreshButton->setObjectName(QStringLiteral("wifiRefreshButton"));
    m_refreshButton->setProperty("role", "secondaryCompact");
    networksCard->contentLayout()->addWidget(m_refreshButton);
    content->addWidget(networksCard, 5);

    auto* connectionCard = new SectionCard(this);
    connectionCard->contentLayout()->addWidget(
        makeLabel(QStringLiteral("连接信息"), "body", connectionCard));
    m_selectedName = makeLabel(QStringLiteral("尚未选择网络"), "body", connectionCard);
    m_selectedName->setObjectName(QStringLiteral("selectedWifiName"));
    m_selectedName->setWordWrap(true);
    connectionCard->contentLayout()->addWidget(m_selectedName);
    m_selectedDetail = makeLabel(QStringLiteral("请从左侧选择"), "assist", connectionCard);
    m_selectedDetail->setObjectName(QStringLiteral("selectedWifiDetail"));
    m_selectedDetail->setWordWrap(true);
    connectionCard->contentLayout()->addWidget(m_selectedDetail);
    m_passwordEdit = new QLineEdit(connectionCard);
    m_passwordEdit->setObjectName(QStringLiteral("wifiPasswordEdit"));
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(QStringLiteral("输入 Wi-Fi 密码"));
    m_passwordEdit->setInputMethodHints(Qt::ImhNoPredictiveText
                                        | Qt::ImhSensitiveData);
    connectionCard->contentLayout()->addWidget(m_passwordEdit);
    m_showPassword = new QCheckBox(QStringLiteral("显示密码"), connectionCard);
    m_showPassword->setObjectName(QStringLiteral("showWifiPassword"));
    connectionCard->contentLayout()->addWidget(m_showPassword);
    m_connectionStatus = makeLabel({}, "assist", connectionCard);
    m_connectionStatus->setObjectName(QStringLiteral("wifiConnectionStatus"));
    m_connectionStatus->setWordWrap(true);
    connectionCard->contentLayout()->addWidget(m_connectionStatus);
    connectionCard->contentLayout()->addStretch();
    m_connectButton = new QPushButton(QStringLiteral("连接"), connectionCard);
    m_connectButton->setObjectName(QStringLiteral("wifiConnectButton"));
    m_connectButton->setProperty("role", "primaryCompact");
    connectionCard->contentLayout()->addWidget(m_connectButton);
    content->addWidget(connectionCard, 4);

    root->addLayout(content, 1);

    connect(m_refreshButton, &QPushButton::clicked,
            this, &NetworkSetupPage::scanRequested);
    connect(m_networkList, &QListWidget::currentRowChanged,
            this, [this] { updateSelection(); });
    connect(m_showPassword, &QCheckBox::toggled, this, [this](bool visible) {
        m_passwordEdit->setEchoMode(visible ? QLineEdit::Normal
                                            : QLineEdit::Password);
    });
    connect(m_connectButton, &QPushButton::clicked,
            this, &NetworkSetupPage::requestConnection);
    connect(m_passwordEdit, &QLineEdit::returnPressed,
            this, &NetworkSetupPage::requestConnection);
    updateControls();
}

void NetworkSetupPage::setScanStarted()
{
    m_scanning = true;
    m_scanStatus->setText(QStringLiteral("正在扫描附近网络…"));
    m_connectionStatus->clear();
    updateControls();
}

void NetworkSetupPage::setNetworks(const QList<WifiNetwork>& networks)
{
    const WifiNetwork* previous = selectedNetwork();
    const QString selectedSsid = previous ? previous->ssid : QString();
    m_scanning = false;
    m_networks = networks;
    m_networkList->clear();
    int selectedRow = -1;
    for (int index = 0; index < m_networks.size(); ++index) {
        const WifiNetwork& network = m_networks.at(index);
        auto* item = new QListWidgetItem(networkItemText(network), m_networkList);
        item->setData(Qt::UserRole, index);
        item->setSizeHint(QSize(0, 64));
        if ((!selectedSsid.isEmpty() && network.ssid == selectedSsid)
            || (selectedSsid.isEmpty() && network.connected)) {
            selectedRow = index;
        }
    }
    m_scanStatus->setText(networks.isEmpty()
        ? QStringLiteral("未发现可见 Wi-Fi 网络")
        : QStringLiteral("发现 %1 个网络").arg(networks.size()));
    if (selectedRow >= 0)
        m_networkList->setCurrentRow(selectedRow);
    else if (!m_networks.isEmpty())
        m_networkList->setCurrentRow(0);
    else
        updateSelection();
    updateControls();
}

void NetworkSetupPage::setScanFailed(const QString& error)
{
    m_scanning = false;
    m_scanStatus->setText(QStringLiteral("扫描失败：%1").arg(error));
    updateControls();
}

void NetworkSetupPage::setConnectionStarted(const QString& ssid)
{
    m_connecting = true;
    m_connectionStatus->setText(QStringLiteral("正在连接 %1…").arg(ssid));
    updateControls();
}

void NetworkSetupPage::setConnectionSucceeded(const QString& ssid)
{
    m_connecting = false;
    clearPassword();
    m_connectionStatus->setText(QStringLiteral("已连接到 %1").arg(ssid));
    for (WifiNetwork& network : m_networks)
        network.connected = network.ssid == ssid;
    for (int row = 0; row < m_networkList->count(); ++row) {
        QListWidgetItem* item = m_networkList->item(row);
        const int index = item->data(Qt::UserRole).toInt();
        if (index >= 0 && index < m_networks.size())
            item->setText(networkItemText(m_networks.at(index)));
    }
    updateControls();
}

void NetworkSetupPage::setConnectionFailed(const QString& ssid,
                                           const QString& error)
{
    m_connecting = false;
    clearPassword();
    m_connectionStatus->setText(QStringLiteral("连接 %1 失败：%2")
                                    .arg(ssid, error));
    updateControls();
}

const WifiNetwork* NetworkSetupPage::selectedNetwork() const
{
    const QListWidgetItem* item = m_networkList->currentItem();
    if (!item)
        return nullptr;
    const int index = item->data(Qt::UserRole).toInt();
    if (index < 0 || index >= m_networks.size())
        return nullptr;
    return &m_networks.at(index);
}

void NetworkSetupPage::updateSelection()
{
    clearPassword();
    m_connectionStatus->clear();
    const WifiNetwork* network = selectedNetwork();
    if (!network) {
        m_selectedName->setText(QStringLiteral("尚未选择网络"));
        m_selectedDetail->setText(QStringLiteral("请从左侧选择"));
    } else {
        m_selectedName->setText(network->ssid);
        m_selectedDetail->setText(QStringLiteral("信号 %1% · %2")
                                      .arg(network->signalStrength)
                                      .arg(securityText(*network)));
    }
    updateControls();
}

void NetworkSetupPage::updateControls()
{
    const WifiNetwork* network = selectedNetwork();
    const bool idle = !m_scanning && !m_connecting;
    const bool canConnect = idle && network && network->supported
        && !network->connected;
    const bool needsPassword = network && network->requiresPassword;
    m_networkList->setEnabled(idle);
    m_refreshButton->setEnabled(idle);
    m_passwordEdit->setEnabled(canConnect && needsPassword);
    m_showPassword->setEnabled(canConnect && needsPassword);
    m_connectButton->setEnabled(canConnect);
    if (network && network->connected)
        m_connectButton->setText(QStringLiteral("当前已连接"));
    else if (m_connecting)
        m_connectButton->setText(QStringLiteral("连接中…"));
    else
        m_connectButton->setText(QStringLiteral("连接"));
    if (network && !needsPassword)
        m_passwordEdit->setPlaceholderText(QStringLiteral("开放网络无需密码"));
    else
        m_passwordEdit->setPlaceholderText(QStringLiteral("输入 Wi-Fi 密码"));
}

void NetworkSetupPage::requestConnection()
{
    const WifiNetwork* network = selectedNetwork();
    if (!network || !m_connectButton->isEnabled())
        return;
    QString password = m_passwordEdit->text();
    clearPassword();
    emit connectionRequested(*network, password);
    password.fill(QChar());
    password.clear();
}

void NetworkSetupPage::clearPassword()
{
    m_passwordEdit->clear();
    m_passwordEdit->clearFocus();
    m_showPassword->setChecked(false);
}
