#pragma once

#include "model/NetworkModels.h"

#include <QObject>
#include <QPointer>
#include <QString>

class QProcess;

class NetworkManagerAdapter final : public QObject {
    Q_OBJECT

public:
    explicit NetworkManagerAdapter(QObject* parent = nullptr);
    NetworkManagerAdapter(const QString& executable,
                          const QString& interfaceName,
                          QObject* parent = nullptr);
    ~NetworkManagerAdapter() override;

    bool isBusy() const;
    QString interfaceName() const;

    static QList<WifiNetwork> parseScanOutput(const QByteArray& output);

public slots:
    void scanNetworks();
    void connectToNetwork(const WifiNetwork& network, const QString& password);

signals:
    void scanStarted();
    void scanCompleted(const QList<WifiNetwork>& networks);
    void scanFailed(const QString& error);
    void connectionStarted(const QString& ssid);
    void connectionSucceeded(const QString& ssid);
    void connectionFailed(const QString& ssid, const QString& error);

private:
    QProcess* createProcess();
    void stopProcesses();
    QString friendlyError(const QByteArray& output,
                          const QString& fallback) const;

    QString m_executable;
    QString m_interfaceName;
    QPointer<QProcess> m_scanProcess;
    QPointer<QProcess> m_connectProcess;
};
