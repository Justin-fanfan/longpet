#include "MotionService.h"
#include <QDebug>
#include <QByteArray>
#include <QDateTime>

MotionService::MotionService(QObject* parent) : QObject(parent) {
    // 连接错误信号
    connect(&m_serialPort, &QSerialPort::errorOccurred,
            this, &MotionService::handleError);
}

MotionService::~MotionService() {
    if (m_serialPort.isOpen())
        m_serialPort.close();
}

bool MotionService::openSerialPort(const QString& portName, qint32 baudRate) {
    if (m_serialPort.isOpen())
        m_serialPort.close();

    m_serialPort.setPortName(portName);
    m_serialPort.setBaudRate(baudRate);
    m_serialPort.setDataBits(QSerialPort::Data8);
    m_serialPort.setParity(QSerialPort::NoParity);
    m_serialPort.setStopBits(QSerialPort::OneStop);
    m_serialPort.setFlowControl(QSerialPort::NoFlowControl);

    if (!m_serialPort.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to open serial port" << portName << m_serialPort.errorString();
        m_serialOpen = false;
        return false;
    }
    m_serialOpen = true;
    qDebug() << "Serial port opened:" << portName << baudRate;
    return true;
}

void MotionService::onPoseDataReceived(const PoseData& pose) {
    if (!m_serialOpen) {
        qWarning() << "Serial port not open, cannot send pose data.";
        return;
    }

    // ---- 自定义协议打包 ----
    // 格式：$POSE,<distance>,<dx>,<dy>,<confidence>*<checksum>\n
    // 示例：$POSE,45.6,12.3,-8.7,0.98*5A\n
    QString msg = QString("%1 %2 %3")
                      .arg(pose.dx, 0, 'f', 0)
                      .arg(pose.dy, 0, 'f', 0)
                      .arg(pose.distance, 0, 'f', 0);

    // 计算校验和（简单异或）
    quint8 checksum = 0;
    for (int i = 0; i < msg.size(); ++i) {
        checksum ^= static_cast<quint8>(msg[i].toLatin1());
    }
    msg += QString("*%1\n").arg(checksum, 2, 16, QChar('0')).toUpper();

    QByteArray data = msg.toUtf8();
    sendData(data);
}

void MotionService::sendData(const QByteArray& data) {
    if (!m_serialOpen) return;
    qint64 written = m_serialPort.write(data);
    if (written == -1) {
        qWarning() << "Serial write error:" << m_serialPort.errorString();
    } else {
        // 可选：flush 确保立即发送
        m_serialPort.flush();
    }
}

void MotionService::handleError(QSerialPort::SerialPortError error) {
    if (error != QSerialPort::NoError) {
        qWarning() << "Serial port error:" << error << m_serialPort.errorString();
        // 可尝试重新连接
    }
}