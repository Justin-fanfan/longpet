#include "EspSerialAdapter.h"

#include "platform/EspMotionProtocol.h"

#include <QFile>
#include <QSocketNotifier>

#include <cerrno>
#include <cstring>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace {
constexpr int ReconnectIntervalMs = 2'000;
constexpr qsizetype MaximumReadBuffer = 4 * 1024;
}

EspSerialAdapter::EspSerialAdapter(QObject* parent)
    : MotionPort(parent)
{
    m_reconnectTimer.setInterval(ReconnectIntervalMs);
    connect(&m_reconnectTimer, &QTimer::timeout,
            this, &EspSerialAdapter::retryOpen);
}

EspSerialAdapter::~EspSerialAdapter()
{
    stop();
}

bool EspSerialAdapter::start(const QString& device, int baudRate,
                             QString* error)
{
    if (device.trimmed().isEmpty() || baudRate != 115'200) {
        if (error) {
            *error = device.trimmed().isEmpty()
                ? QStringLiteral("Motion UART 设备路径为空")
                : QStringLiteral("Motion MCU 当前只支持 115200 波特率");
        }
        return false;
    }
    if (m_running && m_device == device && m_baudRate == baudRate)
        return true;
    stop();
    m_device = device;
    m_baudRate = baudRate;
    m_running = true;
    retryOpen();
    if (!isTransportAvailable() && error) {
        *error = QStringLiteral("暂时无法打开 %1，将自动重连").arg(m_device);
    }
    return true;
}

void EspSerialAdapter::stop()
{
    m_running = false;
    m_reconnectTimer.stop();
    closeTransport(QStringLiteral("Motion UART 已停止"));
    m_readBuffer.clear();
}

bool EspSerialAdapter::isTransportAvailable() const
{
    return m_descriptor >= 0;
}

bool EspSerialAdapter::sendStop(QString* error)
{
    return sendBytes(EspMotionProtocol::stopCommand(), error);
}

bool EspSerialAdapter::sendMode(MotionControlMode mode, QString* error)
{
    return sendBytes(EspMotionProtocol::modeCommand(mode), error);
}

bool EspSerialAdapter::sendMove(ChassisMotion motion, int speed,
                                QString* error)
{
    return sendBytes(EspMotionProtocol::moveCommand(motion, speed), error);
}

bool EspSerialAdapter::sendHead(HeadMotion motion, int stepUs,
                                QString* error)
{
    return sendBytes(EspMotionProtocol::headCommand(motion, stepUs), error);
}

bool EspSerialAdapter::requestStatus(QString* error)
{
    return sendBytes(EspMotionProtocol::statusCommand(), error);
}

void EspSerialAdapter::retryOpen()
{
    if (!m_running || isTransportAvailable()) {
        m_reconnectTimer.stop();
        return;
    }
#ifdef Q_OS_UNIX
    const QByteArray path = QFile::encodeName(m_device);
    const int descriptor = ::open(path.constData(),
                                  O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (descriptor < 0) {
        const QString detail = QStringLiteral("无法打开 %1：%2")
            .arg(m_device, QString::fromLocal8Bit(std::strerror(errno)));
        emit transportAvailabilityChanged(false, detail);
        m_reconnectTimer.start();
        return;
    }

    termios options {};
    if (::tcgetattr(descriptor, &options) != 0) {
        const QString detail = QStringLiteral("读取 %1 串口参数失败：%2")
            .arg(m_device, QString::fromLocal8Bit(std::strerror(errno)));
        ::close(descriptor);
        emit transportAvailabilityChanged(false, detail);
        m_reconnectTimer.start();
        return;
    }
    ::cfmakeraw(&options);
    ::cfsetispeed(&options, B115200);
    ::cfsetospeed(&options, B115200);
    options.c_cflag |= CLOCAL | CREAD;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
#ifdef CRTSCTS
    options.c_cflag &= ~CRTSCTS;
#endif
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 0;
    if (::tcsetattr(descriptor, TCSANOW, &options) != 0
        || ::tcflush(descriptor, TCIOFLUSH) != 0) {
        const QString detail = QStringLiteral("配置 %1 为 115200 8N1 失败：%2")
            .arg(m_device, QString::fromLocal8Bit(std::strerror(errno)));
        ::close(descriptor);
        emit transportAvailabilityChanged(false, detail);
        m_reconnectTimer.start();
        return;
    }

    m_descriptor = descriptor;
    m_readNotifier = new QSocketNotifier(
        m_descriptor, QSocketNotifier::Read, this);
    connect(m_readNotifier, &QSocketNotifier::activated,
            this, [this] { readAvailable(); });
    m_reconnectTimer.stop();
    emit transportAvailabilityChanged(
        true, QStringLiteral("%1 · 115200 8N1").arg(m_device));
#else
    emit transportAvailabilityChanged(
        false, QStringLiteral("当前平台不支持板端 UART"));
    m_reconnectTimer.start();
#endif
}

void EspSerialAdapter::readAvailable()
{
#ifdef Q_OS_UNIX
    if (m_descriptor < 0)
        return;
    char bytes[512];
    for (;;) {
        const ssize_t count = ::read(m_descriptor, bytes, sizeof(bytes));
        if (count > 0) {
            m_readBuffer.append(bytes, static_cast<qsizetype>(count));
            continue;
        }
        // With VMIN=0/VTIME=0 a tty may return 0 after all currently
        // available bytes have been consumed.  That is not EOF: closing here
        // makes a healthy MCU connection flap on every STATUS response.
        if (count == 0)
            break;
        if (errno == EINTR)
            continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            closeTransport(QStringLiteral("Motion UART 读取失败：%1")
                               .arg(QString::fromLocal8Bit(std::strerror(errno))));
            if (m_running)
                m_reconnectTimer.start();
        }
        break;
    }
    if (m_readBuffer.size() > MaximumReadBuffer) {
        m_readBuffer = m_readBuffer.right(MaximumReadBuffer);
        const qsizetype newline = m_readBuffer.indexOf('\n');
        if (newline >= 0)
            m_readBuffer.remove(0, newline + 1);
    }
    for (;;) {
        qsizetype end = m_readBuffer.indexOf('\n');
        const qsizetype carriage = m_readBuffer.indexOf('\r');
        if (end < 0 || (carriage >= 0 && carriage < end))
            end = carriage;
        if (end < 0)
            break;
        const QByteArray line = m_readBuffer.left(end).trimmed();
        qsizetype remove = end + 1;
        while (remove < m_readBuffer.size()
               && (m_readBuffer.at(remove) == '\r'
                   || m_readBuffer.at(remove) == '\n')) {
            ++remove;
        }
        m_readBuffer.remove(0, remove);
        if (!line.isEmpty())
            processLine(line);
    }
#endif
}

void EspSerialAdapter::processLine(const QByteArray& line)
{
    emit mcuActivity();
    MotionStatusSnapshot status;
    if (EspMotionProtocol::parseStatusLine(line, &status)) {
        status.uartAvailable = true;
        status.mcuOnline = true;
        emit statusReceived(status);
        return;
    }
    if (line.startsWith(QByteArrayLiteral("[FAULT] "))) {
        emit faultReported(QString::fromLatin1(line.mid(8)).trimmed());
    }
}

bool EspSerialAdapter::sendBytes(const QByteArray& bytes, QString* error)
{
    if (bytes.isEmpty()) {
        if (error)
            *error = QStringLiteral("Motion MCU 指令参数无效");
        return false;
    }
#ifdef Q_OS_UNIX
    if (m_descriptor < 0) {
        if (error)
            *error = QStringLiteral("Motion UART 未连接");
        return false;
    }
    qsizetype written = 0;
    while (written < bytes.size()) {
        const ssize_t count = ::write(m_descriptor, bytes.constData() + written,
                                      static_cast<size_t>(bytes.size() - written));
        if (count > 0) {
            written += static_cast<qsizetype>(count);
            continue;
        }
        const QString detail = QStringLiteral("Motion UART 写入失败：%1")
            .arg(QString::fromLocal8Bit(std::strerror(errno)));
        if (error)
            *error = detail;
        closeTransport(detail);
        if (m_running)
            m_reconnectTimer.start();
        return false;
    }
    return true;
#else
    if (error)
        *error = QStringLiteral("当前平台不支持板端 UART");
    return false;
#endif
}

void EspSerialAdapter::closeTransport(const QString& detail)
{
    const bool wasAvailable = m_descriptor >= 0;
    if (m_readNotifier) {
        m_readNotifier->setEnabled(false);
        m_readNotifier->deleteLater();
        m_readNotifier.clear();
    }
#ifdef Q_OS_UNIX
    if (m_descriptor >= 0)
        ::close(m_descriptor);
#endif
    m_descriptor = -1;
    m_readBuffer.clear();
    if (wasAvailable)
        emit transportAvailabilityChanged(false, detail);
}
