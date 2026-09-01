#include "CameraCaptureAdapter.h"

#include <QDateTime>
#include <QDebug>

namespace {
constexpr qsizetype MaximumCameraBufferBytes = 4 * 1024 * 1024;
const QByteArray JpegStartMarker = QByteArray::fromHex("ffd8");
const QByteArray JpegEndMarker = QByteArray::fromHex("ffd9");
}

CameraCaptureAdapter::CameraCaptureAdapter(QObject* parent)
    : CameraSourcePort(parent)
{
    qRegisterMetaType<CameraFrame>();
    connect(&m_cameraProcess, &QProcess::readyReadStandardOutput,
            this, &CameraCaptureAdapter::processCameraOutput);
    connect(&m_cameraProcess,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            &CameraCaptureAdapter::handleProcessFinished);
}

CameraCaptureAdapter::~CameraCaptureAdapter()
{
    m_consumers.clear();
    m_destroyConnections.clear();
    if (m_running || m_cameraProcess.state() != QProcess::NotRunning)
        stopCapture();
}

bool CameraCaptureAdapter::acquire(QObject* consumer, QString* error)
{
    if (!consumer) {
        if (error)
            *error = QStringLiteral("摄像头 consumer 不能为空");
        return false;
    }

    if (!m_running) {
        QString startError;
        if (!startCapture(&startError)) {
            const QString message = startError.isEmpty()
                ? QStringLiteral("摄像头启动失败") : startError;
            if (error)
                *error = message;
            emit availabilityChanged(false, message);
            emit failed(QStringLiteral("CAMERA_UNAVAILABLE"), message);
            return false;
        }
        m_running = true;
        emit availabilityChanged(true,
                                 QStringLiteral("摄像头已就绪：%1")
                                     .arg(configuredDevice()));
    }

    if (!m_consumers.contains(consumer)) {
        m_consumers.insert(consumer);
        m_destroyConnections.insert(
            consumer,
            connect(consumer, &QObject::destroyed, this,
                    [this](QObject* destroyedConsumer) {
                releaseConsumer(destroyedConsumer, false);
            }));
    }
    qInfo() << "Camera acquired; consumers=" << m_consumers.size();
    return true;
}

void CameraCaptureAdapter::release(QObject* consumer)
{
    releaseConsumer(consumer, true);
}

void CameraCaptureAdapter::releaseConsumer(
    QObject* consumer, bool disconnectDestroyedSignal)
{
    if (!consumer || !m_consumers.remove(consumer))
        return;

    const auto connection = m_destroyConnections.take(consumer);
    if (disconnectDestroyedSignal)
        disconnect(connection);
    qInfo() << "Camera released; consumers=" << m_consumers.size();
    if (!m_consumers.isEmpty() || !m_running)
        return;

    stopCapture();
    m_running = false;
    m_cameraBuffer.clear();
    emit availabilityChanged(false, QStringLiteral("摄像头当前无消费者"));
}

bool CameraCaptureAdapter::isAvailable() const
{
    return m_running;
}

int CameraCaptureAdapter::consumerCount() const
{
    return m_consumers.size();
}

CameraFrame CameraCaptureAdapter::latestFrame() const
{
    return m_latestFrame;
}

QString CameraCaptureAdapter::configuredDevice()
{
    const QString sharedDevice =
        qEnvironmentVariable("LONGPET_CAMERA_DEVICE").trimmed();
    if (!sharedDevice.isEmpty())
        return sharedDevice;
    const QString legacyCallDevice =
        qEnvironmentVariable("LONGPET_CALL_CAMERA_DEVICE").trimmed();
    return legacyCallDevice.isEmpty()
        ? QStringLiteral("/dev/video0") : legacyCallDevice;
}

bool CameraCaptureAdapter::startCapture(QString* error)
{
#ifndef Q_OS_LINUX
    if (error)
        *error = QStringLiteral("摄像头采集仅支持 LongPet Linux 设备");
    return false;
#else
    if (m_cameraProcess.state() != QProcess::NotRunning)
        return true;

    const QString caps =
        QStringLiteral("image/jpeg,width=640,height=480,framerate=30/1");
    m_cameraProcess.setProcessChannelMode(QProcess::SeparateChannels);
    m_cameraProcess.setProgram(QStringLiteral("gst-launch-1.0"));
    m_cameraProcess.setArguments({
        QStringLiteral("-q"), QStringLiteral("v4l2src"),
        QStringLiteral("device=%1").arg(configuredDevice()),
        QStringLiteral("!"), caps, QStringLiteral("!"),
        QStringLiteral("fdsink"), QStringLiteral("fd=1"),
        QStringLiteral("sync=false")
    });
    m_stoppingProcess = false;
    m_cameraProcess.start();
    if (!m_cameraProcess.waitForStarted(1'500)) {
        if (error) {
            *error = QStringLiteral("摄像头采集启动失败：%1")
                         .arg(m_cameraProcess.errorString());
        }
        return false;
    }
    qInfo().noquote()
        << QStringLiteral("Shared camera started: %1, 640x480 MJPEG @ 30 FPS")
               .arg(configuredDevice());
    return true;
#endif
}

void CameraCaptureAdapter::stopCapture()
{
    m_stoppingProcess = true;
    if (m_cameraProcess.state() != QProcess::NotRunning) {
        m_cameraProcess.terminate();
        if (!m_cameraProcess.waitForFinished(400)) {
            m_cameraProcess.kill();
            m_cameraProcess.waitForFinished(800);
        }
    }
    m_stoppingProcess = false;
}

void CameraCaptureAdapter::ingestCameraBytes(const QByteArray& bytes)
{
    if (bytes.isEmpty())
        return;
    m_cameraBuffer.append(bytes);

    while (true) {
        const qsizetype start = m_cameraBuffer.indexOf(JpegStartMarker);
        if (start < 0) {
            if (m_cameraBuffer.size() > MaximumCameraBufferBytes) {
                const bool endsWithPrefix = m_cameraBuffer.endsWith(char(0xff));
                m_cameraBuffer = endsWithPrefix
                    ? QByteArray(1, char(0xff)) : QByteArray();
            }
            return;
        }
        if (start > 0)
            m_cameraBuffer.remove(0, start);

        const qsizetype end = m_cameraBuffer.indexOf(JpegEndMarker, 2);
        if (end < 0) {
            if (m_cameraBuffer.size() > MaximumCameraBufferBytes)
                m_cameraBuffer.clear();
            return;
        }

        QByteArray jpeg = m_cameraBuffer.left(end + JpegEndMarker.size());
        m_cameraBuffer.remove(0, end + JpegEndMarker.size());
        publishFrame(std::move(jpeg));
    }
}

void CameraCaptureAdapter::processCameraOutput()
{
    ingestCameraBytes(m_cameraProcess.readAllStandardOutput());
}

void CameraCaptureAdapter::handleProcessFinished(int exitCode,
                                                 QProcess::ExitStatus)
{
    if (m_stoppingProcess)
        return;

    m_running = false;
    const QString diagnostic =
        QString::fromLocal8Bit(m_cameraProcess.readAllStandardError()).trimmed();
    const QString message = diagnostic.isEmpty()
        ? QStringLiteral("摄像头采集异常退出（%1）").arg(exitCode)
        : QStringLiteral("摄像头采集失败：%1").arg(diagnostic.left(300));
    qWarning().noquote() << message;
    emit availabilityChanged(false, message);
    emit failed(QStringLiteral("CAMERA_UNAVAILABLE"), message);
}

void CameraCaptureAdapter::publishFrame(QByteArray jpeg)
{
    CameraFrame frame;
    frame.jpeg = std::move(jpeg);
    frame.sequence = ++m_nextSequence;
    frame.timestamp = QDateTime::currentDateTimeUtc();
    m_latestFrame = frame;
    emit frameReady(frame);
}
