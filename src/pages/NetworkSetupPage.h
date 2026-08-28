#pragma once

#include "model/NetworkModels.h"

#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

class NetworkSetupPage final : public QWidget {
    Q_OBJECT

public:
    explicit NetworkSetupPage(QWidget* parent = nullptr);

    void setScanStarted();
    void setNetworks(const QList<WifiNetwork>& networks);
    void setScanFailed(const QString& error);
    void setConnectionStarted(const QString& ssid);
    void setConnectionSucceeded(const QString& ssid);
    void setConnectionFailed(const QString& ssid, const QString& error);

signals:
    void backRequested();
    void scanRequested();
    void connectionRequested(const WifiNetwork& network,
                             const QString& password);

private:
    const WifiNetwork* selectedNetwork() const;
    void updateSelection();
    void updateControls();
    void requestConnection();
    void clearPassword();

    QList<WifiNetwork> m_networks;
    QListWidget* m_networkList = nullptr;
    QLabel* m_scanStatus = nullptr;
    QLabel* m_selectedName = nullptr;
    QLabel* m_selectedDetail = nullptr;
    QLabel* m_connectionStatus = nullptr;
    QLineEdit* m_passwordEdit = nullptr;
    QCheckBox* m_showPassword = nullptr;
    QPushButton* m_refreshButton = nullptr;
    QPushButton* m_connectButton = nullptr;
    bool m_scanning = false;
    bool m_connecting = false;
};
