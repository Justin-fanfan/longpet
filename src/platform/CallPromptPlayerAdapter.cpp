#include "CallPromptPlayerAdapter.h"

#include <QDir>
#include <QFile>
#include <QLoggingCategory>

namespace {
QString playbackDevice()
{
    const QString configured = qEnvironmentVariable("LONGPET_CALL_PLAYBACK_DEVICE").trimmed();
    return configured.isEmpty()
        ? QStringLiteral("plughw:CARD=Device,DEV=0") : configured;
}
}

CallPromptPlayerAdapter::CallPromptPlayerAdapter(QObject* parent)
    : CallPromptPlayerPort(parent)
{
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus status) {
        clearTemporaryFile();
        if (m_stopping)
            return;
        if (status == QProcess::NormalExit && exitCode == 0)
            emit finished();
        else
            emit failed(QStringLiteral("提示音播放进程异常退出（%1）").arg(exitCode));
    });
    connect(&m_process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError) {
        if (!m_stopping)
            emit failed(QStringLiteral("提示音播放失败：%1").arg(m_process.errorString()));
    });
}

CallPromptPlayerAdapter::~CallPromptPlayerAdapter()
{
    stop();
}

bool CallPromptPlayerAdapter::play(VideoCallMode mode, QString* error)
{
    stop();
    m_stopping = false;
    const QString resource = mode == VideoCallMode::Video
        ? QStringLiteral(":/sounds/zh_video_call.wav")
        : QStringLiteral(":/sounds/zh_voice_call.wav");
    QFile source(resource);
    if (!source.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("提示音资源不存在：%1").arg(resource);
        return false;
    }

    m_temporaryFile = std::make_unique<QTemporaryFile>(
        QDir::temp().filePath(QStringLiteral("longpet-call-prompt-XXXXXX.wav")));
    m_temporaryFile->setAutoRemove(true);
    if (!m_temporaryFile->open()
        || m_temporaryFile->write(source.readAll()) < 0
        || !m_temporaryFile->flush()) {
        if (error)
            *error = QStringLiteral("无法准备临时提示音文件");
        clearTemporaryFile();
        return false;
    }
    const QString path = m_temporaryFile->fileName();
    m_temporaryFile->close();

    m_process.setProgram(QStringLiteral("aplay"));
    m_process.setArguments({QStringLiteral("-q"), QStringLiteral("-D"),
                            playbackDevice(), path});
    m_process.start();
    if (!m_process.waitForStarted(1'000)) {
        if (error)
            *error = QStringLiteral("无法启动 aplay：%1").arg(m_process.errorString());
        clearTemporaryFile();
        return false;
    }
    qInfo() << "Call prompt playing once:" << resource << "on" << playbackDevice();
    return true;
}

void CallPromptPlayerAdapter::stop()
{
    m_stopping = true;
    if (m_process.state() != QProcess::NotRunning) {
        m_process.terminate();
        if (!m_process.waitForFinished(300)) {
            m_process.kill();
            m_process.waitForFinished(500);
        }
    }
    clearTemporaryFile();
}

void CallPromptPlayerAdapter::clearTemporaryFile()
{
    m_temporaryFile.reset();
}
