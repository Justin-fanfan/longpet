#include "VideoCallMediaAdapter.h"

#include "services/CameraPorts.h"

#include <QDateTime>
#include <QHostAddress>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QWebSocket>

#include <algorithm>

namespace {
constexpr qsizetype AudioChunkBytes = 640;
constexpr qint64 MaximumSocketBacklog = 256 * 1024;
constexpr qint64 RemoteVideoMinimumIntervalMs = 120;

QString captureDevice()
{
    const QString configured = qEnvironmentVariable("LONGPET_CALL_CAPTURE_DEVICE").trimmed();
    return configured.isEmpty()
        ? QStringLiteral("plughw:CARD=Device,DEV=0") : configured;
}

QString playbackDevice()
{
    const QString configured = qEnvironmentVariable("LONGPET_CALL_PLAYBACK_DEVICE").trimmed();
    return configured.isEmpty()
        ? QStringLiteral("plughw:CARD=Device,DEV=0") : configured;
}

QHostAddress mediaListenAddress()
{
    const QString configured = qEnvironmentVariable("LONGPET_MEDIA_ADDRESS").trimmed();
    QHostAddress address;
    if (!configured.isEmpty() && address.setAddress(configured))
        return address;
    return QHostAddress(QHostAddress::AnyIPv4);
}
}

VideoCallMediaAdapter::VideoCallMediaAdapter(CameraSourcePort* cameraSource,
                                             QObject* parent)
    : VideoCallMediaPort(parent),
      m_server(QStringLiteral("LongPet media"), QWebSocketServer::NonSecureMode, this),
      m_cameraSource(cameraSource)
{
    connect(&m_server, &QWebSocketServer::newConnection,
            this, &VideoCallMediaAdapter::acceptPendingConnection);
    connect(&m_captureProcess, &QProcess::readyReadStandardOutput,
            this, &VideoCallMediaAdapter::processAudioCaptureOutput);
    if (m_cameraSource) {
        connect(m_cameraSource, &CameraSourcePort::frameReady,
                this, &VideoCallMediaAdapter::handleCameraFrame);
        connect(m_cameraSource, &CameraSourcePort::failed, this,
                [this](const QString& code, const QString& message) {
            if (m_cameraAcquired && m_prepared && !m_stopping)
                emit failed(code, message);
        });
    }

    m_videoFlushTimer.setInterval(20);
    connect(&m_videoFlushTimer, &QTimer::timeout,
            this, &VideoCallMediaAdapter::flushPendingVideo);
    m_audioPlaybackTimer.setInterval(20);
    connect(&m_audioPlaybackTimer, &QTimer::timeout,
            this, &VideoCallMediaAdapter::drainAudioPlayback);

    connect(&m_captureProcess,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) {
        reportProcessFailure(QStringLiteral("MICROPHONE_UNAVAILABLE"),
                             QStringLiteral("麦克风采集"), &m_captureProcess, code);
    });
    connect(&m_playbackProcess,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) {
        reportProcessFailure(QStringLiteral("SPEAKER_UNAVAILABLE"),
                             QStringLiteral("扬声器播放"), &m_playbackProcess, code);
    });
}

VideoCallMediaAdapter::~VideoCallMediaAdapter()
{
    stop();
}

quint16 VideoCallMediaAdapter::configuredPort()
{
    bool valid = false;
    const int value = qEnvironmentVariableIntValue("LONGPET_MEDIA_PORT", &valid);
    if (valid && value > 0 && value <= 65'535)
        return static_cast<quint16>(value);
    return 8'788;
}

quint16 VideoCallMediaAdapter::port() const
{
    return m_server.isListening() ? m_server.serverPort() : configuredPort();
}

bool VideoCallMediaAdapter::prepare(const VideoCallSnapshot& snapshot,
                                    bool audioEnabled, QString* error)
{
    stop();
    m_stopping = false;
    m_session = snapshot;
    m_audioEnabled = audioEnabled;
    if (!m_server.listen(mediaListenAddress(), configuredPort())) {
        if (error)
            *error = QStringLiteral("媒体端口 %1 监听失败：%2")
                         .arg(configuredPort()).arg(m_server.errorString());
        return false;
    }
    m_prepared = true;
    m_videoFlushTimer.start();
    qInfo() << "LongPet media WebSocket listening on"
            << m_server.serverAddress() << m_server.serverPort()
            << "call" << snapshot.callId;

    if (snapshot.mode == VideoCallMode::Video && !startCamera(error)) {
        stop();
        return false;
    }
    if (audioEnabled && !startAudio(error)) {
        stop();
        return false;
    }
    return true;
}

void VideoCallMediaAdapter::enableAudio()
{
    if (!m_prepared || m_audioStarted)
        return;
    m_audioEnabled = true;
    QString error;
    if (!startAudio(&error)) {
        emit failed(QStringLiteral("AUDIO_INITIALIZATION_FAILED"), error);
        return;
    }
    checkReady();
}

void VideoCallMediaAdapter::stop()
{
    m_stopping = true;
    m_videoFlushTimer.stop();
    m_audioPlaybackTimer.stop();
    if (m_socket) {
        disconnect(m_socket, nullptr, this, nullptr);
        m_socket->close(QWebSocketProtocol::CloseCodeNormal,
                        QStringLiteral("通话结束"));
        m_socket->deleteLater();
        m_socket.clear();
    }
    m_server.close();
    stopProcess(&m_captureProcess);
    if (m_cameraAcquired && m_cameraSource) {
        m_cameraSource->release(this);
        m_cameraAcquired = false;
    }
    stopProcess(&m_playbackProcess);
    m_captureBuffer.clear();
    m_pendingLocalVideo.clear();
    m_pendingRemoteVideo.clear();
    m_audioPlaybackQueue.clear();
    std::fill(std::begin(m_sequences), std::end(m_sequences), 0U);
    m_session = {};
    m_cameraFrameCounter = 0;
    m_remoteVideoDecodeClock.invalidate();
    m_prepared = false;
    m_authenticated = false;
    m_audioEnabled = false;
    m_audioStarted = false;
    m_readyEmitted = false;
    m_remoteDecodeScheduled = false;
    m_playbackPrimed = false;
}

void VideoCallMediaAdapter::acceptPendingConnection()
{
    while (m_server.hasPendingConnections()) {
        QWebSocket* socket = m_server.nextPendingConnection();
        if (m_socket) {
            socket->close(QWebSocketProtocol::CloseCodePolicyViolated,
                          QStringLiteral("设备正在使用媒体通道"));
            socket->deleteLater();
            continue;
        }
        m_socket = socket;
        connect(socket, &QWebSocket::binaryMessageReceived,
                this, &VideoCallMediaAdapter::handleBinaryMessage);
        connect(socket, &QWebSocket::disconnected,
                this, &VideoCallMediaAdapter::handleSocketDisconnected);
        qInfo() << "Media peer connected, waiting for session authentication";
    }
}

void VideoCallMediaAdapter::handleBinaryMessage(const QByteArray& message)
{
    MediaFrame frame;
    QString error;
    if (!MediaFrameProtocol::decode(message, &frame, &error)) {
        qWarning() << "Discard invalid media frame:" << error;
        return;
    }
    if (!m_authenticated) {
        if (frame.streamType != MediaStreamType::Control) {
            if (m_socket)
                m_socket->close(QWebSocketProtocol::CloseCodePolicyViolated,
                                QStringLiteral("媒体鉴权尚未完成"));
            return;
        }
        handleControlFrame(frame.payload);
        return;
    }

    switch (frame.streamType) {
    case MediaStreamType::FamilyVideo:
        if (m_session.mode == VideoCallMode::Video
            && (!m_remoteVideoDecodeClock.isValid()
                || m_remoteVideoDecodeClock.elapsed() >= RemoteVideoMinimumIntervalMs)) {
            if (m_remoteVideoDecodeClock.isValid())
                m_remoteVideoDecodeClock.restart();
            else
                m_remoteVideoDecodeClock.start();
            scheduleRemoteVideoDecode(frame.payload);
        }
        break;
    case MediaStreamType::FamilyAudio:
        if (!m_audioStarted)
            break;
        while (m_audioPlaybackQueue.size() >= 12)
            m_audioPlaybackQueue.dequeue();
        m_audioPlaybackQueue.enqueue(frame.payload);
        break;
    case MediaStreamType::Control:
        handleControlFrame(frame.payload);
        break;
    case MediaStreamType::DeviceVideo:
    case MediaStreamType::DeviceAudio:
        break;
    }
}

void VideoCallMediaAdapter::handleControlFrame(const QByteArray& payload)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return;
    const QJsonObject object = document.object();
    const QString type = object.value(QStringLiteral("type")).toString();
    if (!m_authenticated && type == QStringLiteral("authenticate")) {
        const bool accepted = object.value(QStringLiteral("callId")).toString()
                                  == m_session.callId
            && object.value(QStringLiteral("token")).toString()
                                  == m_session.mediaToken
            && !m_session.mediaToken.isEmpty();
        if (!accepted) {
            qWarning() << "Media authentication rejected";
            if (m_socket)
                m_socket->close(QWebSocketProtocol::CloseCodePolicyViolated,
                                QStringLiteral("媒体鉴权失败"));
            return;
        }
        m_authenticated = true;
        sendControl(QStringLiteral("authenticated"), {
            {QStringLiteral("audioEnabled"), m_audioStarted},
            {QStringLiteral("mode"), m_session.mode == VideoCallMode::Video
                ? QStringLiteral("video") : QStringLiteral("voice")}
        });
        emit peerAuthenticated();
        checkReady();
        return;
    }
    if (type == QStringLiteral("hangup"))
        emit disconnected(QStringLiteral("家属端已关闭媒体通道"));
}

void VideoCallMediaAdapter::handleSocketDisconnected()
{
    QWebSocket* socket = qobject_cast<QWebSocket*>(sender());
    if (socket)
        socket->deleteLater();
    m_socket.clear();
    const bool wasAuthenticated = m_authenticated;
    m_authenticated = false;
    if (!m_stopping && m_prepared && wasAuthenticated)
        emit disconnected(QStringLiteral("与家属端的媒体连接已中断"));
}

bool VideoCallMediaAdapter::startCamera(QString* error)
{
    if (!m_cameraSource) {
        if (error)
            *error = QStringLiteral("共享摄像头服务未配置");
        return false;
    }
    m_cameraAcquired = m_cameraSource->acquire(this, error);
    return m_cameraAcquired;
}

bool VideoCallMediaAdapter::startAudio(QString* error)
{
#ifndef Q_OS_LINUX
    if (error)
        *error = QStringLiteral("USB 音频媒体采集仅支持 LongPet Linux 设备");
    return false;
#else
    const QString caps = QStringLiteral(
        "audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved");
    if (!startProcess(&m_playbackProcess,
        {QStringLiteral("-q"), QStringLiteral("fdsrc"), QStringLiteral("fd=0"),
         QStringLiteral("blocksize=640"), QStringLiteral("!"), caps,
         QStringLiteral("!"), QStringLiteral("queue"),
         QStringLiteral("max-size-time=200000000"), QStringLiteral("leaky=downstream"),
         QStringLiteral("!"), QStringLiteral("audioconvert"), QStringLiteral("!"),
         QStringLiteral("audioresample"), QStringLiteral("!"),
         QStringLiteral("alsasink"),
         QStringLiteral("device=%1").arg(playbackDevice()),
         QStringLiteral("sync=false")}, QStringLiteral("扬声器播放"), error)) {
        return false;
    }
    if (!startProcess(&m_captureProcess,
        {QStringLiteral("-q"), QStringLiteral("alsasrc"),
         QStringLiteral("device=%1").arg(captureDevice()),
         QStringLiteral("provide-clock=false"), QStringLiteral("!"),
         QStringLiteral("audioconvert"), QStringLiteral("!"),
         QStringLiteral("audioresample"), QStringLiteral("!"), caps,
         QStringLiteral("!"), QStringLiteral("fdsink"), QStringLiteral("fd=1"),
         QStringLiteral("sync=false")}, QStringLiteral("麦克风采集"), error)) {
        stopProcess(&m_playbackProcess);
        return false;
    }
    m_audioStarted = true;
    m_audioPlaybackTimer.start();
    return true;
#endif
}

bool VideoCallMediaAdapter::startProcess(QProcess* process,
                                         const QStringList& arguments,
                                         const QString& label, QString* error)
{
    process->setProcessChannelMode(QProcess::SeparateChannels);
    process->setProgram(QStringLiteral("gst-launch-1.0"));
    process->setArguments(arguments);
    process->start();
    if (!process->waitForStarted(1'500)) {
        if (error)
            *error = QStringLiteral("%1启动失败：%2").arg(label, process->errorString());
        return false;
    }
    qInfo() << label << "started with gst-launch-1.0";
    return true;
}

void VideoCallMediaAdapter::stopProcess(QProcess* process)
{
    if (process->state() == QProcess::NotRunning)
        return;
    process->terminate();
    if (!process->waitForFinished(400)) {
        process->kill();
        process->waitForFinished(800);
    }
}

void VideoCallMediaAdapter::handleCameraFrame(const CameraFrame& frame)
{
    if (!m_cameraAcquired || m_session.mode != VideoCallMode::Video
        || !frame.isValid()) {
        return;
    }
    ++m_cameraFrameCounter;
    if ((m_cameraFrameCounter % 3) != 0 || !m_authenticated)
        return;
    if (m_socket && m_socket->bytesToWrite() < MaximumSocketBacklog)
        sendFrame(MediaStreamType::DeviceVideo, frame.jpeg);
    else
        m_pendingLocalVideo = frame.jpeg;
}

void VideoCallMediaAdapter::processAudioCaptureOutput()
{
    m_captureBuffer.append(m_captureProcess.readAllStandardOutput());
    while (m_captureBuffer.size() >= AudioChunkBytes) {
        QByteArray chunk = m_captureBuffer.left(AudioChunkBytes);
        m_captureBuffer.remove(0, AudioChunkBytes);
        if (m_authenticated && m_socket
            && m_socket->bytesToWrite() < MaximumSocketBacklog) {
            sendFrame(MediaStreamType::DeviceAudio, chunk);
        }
    }
}

void VideoCallMediaAdapter::scheduleRemoteVideoDecode(const QByteArray& jpeg)
{
    m_pendingRemoteVideo = jpeg;
    if (m_remoteDecodeScheduled)
        return;
    m_remoteDecodeScheduled = true;
    QTimer::singleShot(0, this, [this] {
        m_remoteDecodeScheduled = false;
        const QByteArray latest = std::move(m_pendingRemoteVideo);
        m_pendingRemoteVideo.clear();
        const QImage image = QImage::fromData(latest, "JPG");
        if (!image.isNull())
            emit remoteVideoFrame(image);
    });
}

void VideoCallMediaAdapter::sendFrame(MediaStreamType streamType,
                                      const QByteArray& payload, quint16 flags)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
        return;
    const auto index = static_cast<int>(streamType);
    const quint32 sequence = ++m_sequences[index];
    const quint64 timestamp = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1'000;
    m_socket->sendBinaryMessage(MediaFrameProtocol::encode(
        streamType, sequence, timestamp, payload, flags));
}

void VideoCallMediaAdapter::sendControl(const QString& type,
                                        const QJsonObject& extra)
{
    QJsonObject object = extra;
    object.insert(QStringLiteral("type"), type);
    sendFrame(MediaStreamType::Control,
              QJsonDocument(object).toJson(QJsonDocument::Compact));
}

void VideoCallMediaAdapter::flushPendingVideo()
{
    if (m_pendingLocalVideo.isEmpty() || !m_socket
        || m_socket->bytesToWrite() >= MaximumSocketBacklog)
        return;
    const QByteArray latest = std::move(m_pendingLocalVideo);
    m_pendingLocalVideo.clear();
    sendFrame(MediaStreamType::DeviceVideo, latest);
}

void VideoCallMediaAdapter::drainAudioPlayback()
{
    if (!m_audioStarted || m_playbackProcess.state() != QProcess::Running)
        return;
    if (!m_playbackPrimed) {
        if (m_audioPlaybackQueue.size() < 3)
            return;
        m_playbackPrimed = true;
    }
    while (m_audioPlaybackQueue.size() > 9)
        m_audioPlaybackQueue.dequeue();
    if (m_audioPlaybackQueue.isEmpty()) {
        m_playbackPrimed = false;
        return;
    }
    if (m_playbackProcess.bytesToWrite() > AudioChunkBytes * 5)
        return;
    m_playbackProcess.write(m_audioPlaybackQueue.dequeue());
}

void VideoCallMediaAdapter::checkReady()
{
    if (!m_readyEmitted && m_authenticated && m_audioStarted) {
        m_readyEmitted = true;
        sendControl(QStringLiteral("media_active"),
                    {{QStringLiteral("audioEnabled"), true}});
        emit mediaReady();
    }
}

void VideoCallMediaAdapter::reportProcessFailure(const QString& code,
                                                 const QString& label,
                                                 QProcess* process,
                                                 int exitCode)
{
    if (m_stopping || !m_prepared)
        return;
    const QString diagnostic = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
    const QString message = diagnostic.isEmpty()
        ? QStringLiteral("%1异常退出（%2）").arg(label).arg(exitCode)
        : QStringLiteral("%1失败：%2").arg(label, diagnostic.left(300));
    qWarning().noquote() << message;
    emit failed(code, message);
}
