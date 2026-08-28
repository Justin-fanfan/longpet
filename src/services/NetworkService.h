#pragma once

#include "model/NetworkModels.h"

#include <QObject>

class NetworkManagerAdapter;

class NetworkService final : public QObject {
    Q_OBJECT

public:
    explicit NetworkService(NetworkManagerAdapter* adapter,
                            QObject* parent = nullptr);

    bool isBusy() const;

public slots:
    void scanNetworks();
    void connectToNetwork(const WifiNetwork& network, const QString& password);

signals:
    void scanStarted();
    void networksChanged(const QList<WifiNetwork>& networks);
    void scanFailed(const QString& error);
    void connectionStarted(const QString& ssid);
    void connectionSucceeded(const QString& ssid);
    void connectionFailed(const QString& ssid, const QString& error);

private:
    NetworkManagerAdapter* m_adapter = nullptr;
};
