#pragma once

#include <QNetworkInformation>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>
#include <optional>

struct NetworkStatusSnapshot {
    bool known = false;
    bool internetAvailable = false;
    QString summary;
};

class NetworkStatusAdapter final : public QObject {
    Q_OBJECT

public:
    using BackendProvider = std::function<QNetworkInformation*()>;

    explicit NetworkStatusAdapter(QObject* parent = nullptr);
    explicit NetworkStatusAdapter(BackendProvider backendProvider,
                                  QObject* parent = nullptr);
    ~NetworkStatusAdapter() override;

    bool start();
    void stop();
    bool isActive() const;
    QString backendName() const;

    static NetworkStatusSnapshot mapState(
        QNetworkInformation::Reachability reachability,
        QNetworkInformation::TransportMedium transportMedium,
        bool behindCaptivePortal = false);

signals:
    void networkStateChanged(bool known, bool internetAvailable,
                             const QString& summary);

private:
    void publishCurrentState();
    void publishStatus(const NetworkStatusSnapshot& status);

    BackendProvider m_backendProvider;
    QPointer<QNetworkInformation> m_networkInformation;
    QString m_backendName;
    std::optional<NetworkStatusSnapshot> m_lastStatus;
};
