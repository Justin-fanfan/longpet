#include "VoiceAudioAdapter.h"

#include <QDataStream>

#include <cmath>

namespace {
QString captureDevice()
{
    const QString configured = qEnvironmentVariable(
        "LONGPET_AI_CAPTURE_DEVICE").trimmed();
    if (!configured.isEmpty())
        return configured;
    const QString callDevice = qEnvironmentVariable(
        "LONGPET_CALL_CAPTURE_DEVICE").trimmed();
    return callDevice.isEmpty()
        ? QStringLiteral("plughw:CARD=Device,DEV=0") : callDevice;
}

QString playbackDevice()
{
    const QString configured = qEnvironmentVariable(
        "LONGPET_AI_PLAYBACK_DEVICE").trimmed();
    if (!configured.isEmpty())
        return configured;
    const QString callDevice = qEnvironmentVariable(
        "LONGPET_CALL_PLAYBACK_DEVICE").trimmed();
    return callDevice.isEmpty()
        ? QStringLiteral("plughw:CARD=Device,DEV=0") : callDevice;
}
}

VoiceAudioAdapter::VoiceAudioAdapter(QObject* parent)
    : VoiceAudioPort(parent)
{
    configureProcess(&m_captureProcess);
    configureProcess(&m_playbackProcess);

    connect(&m_captureProcess, &QProcess::started, this, [this] {
        emit recordingStarted(m_captureSessionId);
    });
    connect(&m_captureProcess, &QProcess::readyReadStandardOutput, this, [this] {
        const QByteArray chunk = m_captureProcess.readAllStandardOutput();
        m_pcm.append(chunk);
        if (m_captureSessionId != 0 && !chunk.isEmpty()) {
            constexpr qint64 bytesPerSecond = 16'000 * 2;
            const qint64 capturedMs = m_pcm.size() * 1'000 / bytesPerSecond;
            emit recordingProgress(m_captureSessionId, capturedMs,
                                   pcmS16LeLevelDb(chunk));
        }
    });
    connect(&m_captureProcess,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &VoiceAudioAdapter::handleCaptureFinished);
    connect(&m_captureProcess, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            reportCaptureFailure(m_captureProcess.errorString());
    });

    connect(&m_playbackProcess, &QProcess::started, this, [this] {
        emit playbackStarted(m_playbackSessionId);
        m_playbackProcess.write(m_playbackAudio);
        m_playbackProcess.closeWriteChannel();
    });
    connect(&m_playbackProcess,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &VoiceAudioAdapter::handlePlaybackFinished);
    connect(&m_playbackProcess, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            reportPlaybackFailure(m_playbackProcess.errorString());
    });

    m_captureKillTimer.setSingleShot(true);
    m_captureKillTimer.setInterval(800);
    connect(&m_captureKillTimer, &QTimer::timeout, this, [this] {
        if (m_captureProcess.state() != QProcess::NotRunning)
            m_captureProcess.kill();
    });
    m_playbackKillTimer.setSingleShot(true);
    m_playbackKillTimer.setInterval(800);
    connect(&m_playbackKillTimer, &QTimer::timeout, this, [this] {
        if (m_playbackProcess.state() != QProcess::NotRunning)
            m_playbackProcess.kill();
    });
}

VoiceAudioAdapter::~VoiceAudioAdapter()
{
    if (m_captureProcess.state() != QProcess::NotRunning) {
        m_captureProcess.kill();
        m_captureProcess.waitForFinished(500);
    }
    if (m_playbackProcess.state() != QProcess::NotRunning) {
        m_playbackProcess.kill();
        m_playbackProcess.waitForFinished(500);
    }
}

void VoiceAudioAdapter::startRecording(quint64 sessionId)
{
    if (m_captureProcess.state() != QProcess::NotRunning
        || m_playbackProcess.state() != QProcess::NotRunning) {
        emit audioFailed(sessionId, VoiceAudioStage::Recording,
                         QStringLiteral("音频设备正在使用，请稍后再试"),
                         QStringLiteral("voice audio process is already running"));
        return;
    }
#ifndef Q_OS_LINUX
    emit audioFailed(sessionId, VoiceAudioStage::Recording,
                     QStringLiteral("当前平台不支持板端麦克风录音"),
                     QStringLiteral("VoiceAudioAdapter recording requires Linux"));
#else
    resetCapture();
    m_captureSessionId = sessionId;
    m_captureProcess.setArguments({
        QStringLiteral("-q"),
        QStringLiteral("alsasrc"),
        QStringLiteral("device=%1").arg(captureDevice()),
        QStringLiteral("provide-clock=false"),
        QStringLiteral("!"), QStringLiteral("audioconvert"),
        QStringLiteral("!"), QStringLiteral("audioresample"),
        QStringLiteral("!"),
        QStringLiteral("audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved"),
        QStringLiteral("!"), QStringLiteral("fdsink"),
        QStringLiteral("fd=1"), QStringLiteral("sync=false")
    });
    m_captureProcess.start();
#endif
}

void VoiceAudioAdapter::finishRecording(quint64 sessionId)
{
    if (sessionId != m_captureSessionId)
        return;
    if (m_captureProcess.state() == QProcess::NotRunning) {
        reportCaptureFailure(QStringLiteral("录音进程未运行"));
        return;
    }
    m_finishingCapture = true;
    m_captureProcess.terminate();
    m_captureKillTimer.start();
}

void VoiceAudioAdapter::play(quint64 sessionId, const QByteArray& audio)
{
    if (audio.isEmpty()) {
        emit audioFailed(sessionId, VoiceAudioStage::Playback,
                         QStringLiteral("没有可播放的语音"),
                         QStringLiteral("playback audio is empty"));
        return;
    }
    if (m_captureProcess.state() != QProcess::NotRunning
        || m_playbackProcess.state() != QProcess::NotRunning) {
        emit audioFailed(sessionId, VoiceAudioStage::Playback,
                         QStringLiteral("音频设备正在使用，请稍后再试"),
                         QStringLiteral("voice audio process is already running"));
        return;
    }
#ifndef Q_OS_LINUX
    emit audioFailed(sessionId, VoiceAudioStage::Playback,
                     QStringLiteral("当前平台不支持板端语音播放"),
                     QStringLiteral("VoiceAudioAdapter playback requires Linux"));
#else
    resetPlayback();
    m_playbackSessionId = sessionId;
    m_playbackAudio = audio;
    m_playbackProcess.setArguments({
        QStringLiteral("-q"),
        QStringLiteral("fdsrc"), QStringLiteral("fd=0"),
        QStringLiteral("!"), QStringLiteral("decodebin"),
        QStringLiteral("!"), QStringLiteral("audioconvert"),
        QStringLiteral("!"), QStringLiteral("audioresample"),
        QStringLiteral("!"), QStringLiteral("alsasink"),
        QStringLiteral("device=%1").arg(playbackDevice()),
        QStringLiteral("sync=true")
    });
    m_playbackProcess.start();
#endif
}

void VoiceAudioAdapter::cancel(quint64 sessionId)
{
    m_pendingCancelSessionId = sessionId;
    bool stopping = false;
    if (sessionId == m_captureSessionId
        && m_captureProcess.state() != QProcess::NotRunning) {
        stopping = true;
        m_cancelingCapture = true;
        m_finishingCapture = false;
        m_captureProcess.terminate();
        m_captureKillTimer.start();
    }
    if (sessionId == m_playbackSessionId
        && m_playbackProcess.state() != QProcess::NotRunning) {
        stopping = true;
        m_cancelingPlayback = true;
        m_playbackProcess.terminate();
        m_playbackKillTimer.start();
    }
    if (!stopping)
        QTimer::singleShot(0, this, &VoiceAudioAdapter::maybeCompleteCancellation);
}

QByteArray VoiceAudioAdapter::pcmS16LeMonoToWav(const QByteArray& pcm,
                                               quint32 sampleRate)
{
    QByteArray wav;
    wav.reserve(44 + pcm.size());
    QDataStream stream(&wav, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + pcm.size());
    stream.writeRawData("WAVE", 4);
    stream.writeRawData("fmt ", 4);
    stream << quint32(16) << quint16(1) << quint16(1);
    stream << sampleRate << quint32(sampleRate * 2) << quint16(2) << quint16(16);
    stream.writeRawData("data", 4);
    stream << quint32(pcm.size());
    stream.writeRawData(pcm.constData(), pcm.size());
    return wav;
}

double VoiceAudioAdapter::pcmS16LeLevelDb(const QByteArray& pcm)
{
    const qsizetype sampleCount = pcm.size() / 2;
    if (sampleCount <= 0)
        return -96.0;

    long double sumSquares = 0.0;
    for (qsizetype i = 0; i < sampleCount; ++i) {
        const int offset = static_cast<int>(i * 2);
        const quint16 raw = static_cast<quint8>(pcm.at(offset))
            | (static_cast<quint16>(static_cast<quint8>(pcm.at(offset + 1))) << 8);
        const qint16 sample = static_cast<qint16>(raw);
        const long double normalized = static_cast<long double>(sample) / 32'768.0L;
        sumSquares += normalized * normalized;
    }
    const long double rms = std::sqrt(sumSquares / sampleCount);
    if (rms <= 0.0000158489L)
        return -96.0;
    return qMax(-96.0, 20.0 * std::log10(static_cast<double>(rms)));
}

void VoiceAudioAdapter::configureProcess(QProcess* process)
{
    process->setProgram(QStringLiteral("gst-launch-1.0"));
    process->setProcessChannelMode(QProcess::SeparateChannels);
}

void VoiceAudioAdapter::handleCaptureFinished(int exitCode,
                                              QProcess::ExitStatus exitStatus)
{
    m_captureKillTimer.stop();
    m_pcm.append(m_captureProcess.readAllStandardOutput());
    const quint64 sessionId = m_captureSessionId;
    const bool canceled = m_cancelingCapture;
    const bool finishedByUser = m_finishingCapture;
    const QString diagnostic = QString::fromLocal8Bit(
        m_captureProcess.readAllStandardError()).trimmed();
    if (!canceled && finishedByUser) {
        const QByteArray wav = pcmS16LeMonoToWav(m_pcm);
        resetCapture();
        emit recordingReady(sessionId, wav);
        maybeCompleteCancellation();
        return;
    }
    if (!canceled && !m_captureFailureReported) {
        reportCaptureFailure(diagnostic.isEmpty()
            ? QStringLiteral("录音进程异常退出（%1/%2）")
                  .arg(exitCode).arg(static_cast<int>(exitStatus))
            : diagnostic);
    }
    resetCapture();
    maybeCompleteCancellation();
}

void VoiceAudioAdapter::handlePlaybackFinished(int exitCode,
                                               QProcess::ExitStatus exitStatus)
{
    m_playbackKillTimer.stop();
    const quint64 sessionId = m_playbackSessionId;
    const bool canceled = m_cancelingPlayback;
    const QString diagnostic = QString::fromLocal8Bit(
        m_playbackProcess.readAllStandardError()).trimmed();
    const bool success = !canceled && exitStatus == QProcess::NormalExit
        && exitCode == 0;
    if (success) {
        resetPlayback();
        emit playbackFinished(sessionId);
        maybeCompleteCancellation();
        return;
    }
    if (!canceled && !m_playbackFailureReported) {
        reportPlaybackFailure(diagnostic.isEmpty()
            ? QStringLiteral("播放进程异常退出（%1/%2）")
                  .arg(exitCode).arg(static_cast<int>(exitStatus))
            : diagnostic);
    }
    resetPlayback();
    maybeCompleteCancellation();
}

void VoiceAudioAdapter::reportCaptureFailure(const QString& diagnostic)
{
    if (m_captureFailureReported)
        return;
    m_captureFailureReported = true;
    emit audioFailed(m_captureSessionId, VoiceAudioStage::Recording,
                     QStringLiteral("麦克风暂时不可用"), diagnostic.left(500));
}

void VoiceAudioAdapter::reportPlaybackFailure(const QString& diagnostic)
{
    if (m_playbackFailureReported)
        return;
    m_playbackFailureReported = true;
    emit audioFailed(m_playbackSessionId, VoiceAudioStage::Playback,
                     QStringLiteral("扬声器播放失败"), diagnostic.left(500));
}

void VoiceAudioAdapter::resetCapture()
{
    m_captureKillTimer.stop();
    m_pcm.clear();
    m_captureSessionId = 0;
    m_finishingCapture = false;
    m_cancelingCapture = false;
    m_captureFailureReported = false;
}

void VoiceAudioAdapter::resetPlayback()
{
    m_playbackKillTimer.stop();
    m_playbackAudio.clear();
    m_playbackSessionId = 0;
    m_cancelingPlayback = false;
    m_playbackFailureReported = false;
}

void VoiceAudioAdapter::maybeCompleteCancellation()
{
    if (m_pendingCancelSessionId == 0
        || m_captureProcess.state() != QProcess::NotRunning
        || m_playbackProcess.state() != QProcess::NotRunning) {
        return;
    }
    const quint64 sessionId = m_pendingCancelSessionId;
    m_pendingCancelSessionId = 0;
    emit cancellationFinished(sessionId);
}
