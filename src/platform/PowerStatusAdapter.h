#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

class PowerStatusAdapter final : public QObject {
    Q_OBJECT

public:
    explicit PowerStatusAdapter(QObject* parent = nullptr);
    explicit PowerStatusAdapter(const QString& sysfsRoot, int refreshIntervalMs = 30'000,
                                QObject* parent = nullptr);

    bool start();
    void stop();
    bool hasBattery() const;
    QString batteryPath() const;

signals:
    void batteryPercentChanged(int percent);
    void powerStateChanged(const QString& summary);

private:
    void refresh();
    QString readText(const QString& path) const;

    QString m_sysfsRoot;
    QString m_batteryPath;
    QTimer m_refreshTimer;
};
