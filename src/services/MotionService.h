#pragma once

#include <QObject>
#include <QSerialPort>
#include "model/VisionModels.h"

class MotionService : public QObject {
    Q_OBJECT
public:
    explicit MotionService(QObject* parent = nullptr);
    ~MotionService();

    // 打开串口（可在初始化时调用）
    bool openSerialPort(const QString& portName, qint32 baudRate = 115200);

public slots:
    // 接收姿态数据
    void onPoseDataReceived(const PoseData& pose);

private:
    void sendData(const QByteArray& data);    // 发送数据
    void handleError(QSerialPort::SerialPortError error);

    QSerialPort m_serialPort;
    bool m_serialOpen = false;
};