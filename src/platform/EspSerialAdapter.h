#pragma once

#include "services/MotionPorts.h"

#include <QByteArray>
#include <QPointer>
#include <QTimer>

class QSocketNotifier;

class EspSerialAdapter final : public MotionPort {
    Q_OBJECT

public:
    explicit EspSerialAdapter(QObject* parent = nullptr);
    ~EspSerialAdapter() override;

    bool start(const QString& device, int baudRate,
               QString* error = nullptr) override;
    void stop() override;
    bool isTransportAvailable() const override;
    bool sendStop(QString* error = nullptr) override;
    bool sendMode(MotionControlMode mode, QString* error = nullptr) override;
    bool sendMove(ChassisMotion motion, int speed,
                  QString* error = nullptr) override;
    bool sendFollowMove(ChassisMotion motion, int speed,
                        QString* error = nullptr) override;
    bool sendHead(HeadMotion motion, int stepUs,
                  QString* error = nullptr) override;
    bool sendTarget(const MotionTargetFrame& target,
                    QString* error = nullptr) override;
    bool requestStatus(QString* error = nullptr) override;

private:
    void retryOpen();
    void readAvailable();
    void processLine(const QByteArray& line);
    bool sendBytes(const QByteArray& bytes, QString* error);
    void closeTransport(const QString& detail);

    QString m_device;
    int m_baudRate = 115'200;
    int m_descriptor = -1;
    QPointer<QSocketNotifier> m_readNotifier;
    QTimer m_reconnectTimer;
    QByteArray m_readBuffer;
    bool m_running = false;
};
