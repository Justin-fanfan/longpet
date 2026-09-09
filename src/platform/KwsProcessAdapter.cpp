#include "KwsProcessAdapter.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QDebug>
#ifdef Q_OS_LINUX
#include <signal.h>
#include <unistd.h>
#endif

namespace {
constexpr int KwsProtocolVersion = 1;

QString number(double value)
{
    return QString::number(value, 'g', 8);
}
}

KwsProcessAdapter::KwsProcessAdapter(
    const KwsConfiguration& configuration, QObject* parent)
    : KwsPort(parent), m_configuration(configuration)
{
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
#ifdef Q_OS_LINUX
    m_process.setChildProcessModifier([] {
        if (::setsid() == -1)
            ::_exit(127);
    });
#endif
    connect(&m_process, &QProcess::started, this, [this] {
        m_processGroup = m_process.processId();
    });
    connect(&m_process, &QProcess::readyReadStandardOutput,
            this, &KwsProcessAdapter::consumeOutput);
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        const QString diagnostic = QString::fromLocal8Bit(
            m_process.readAllStandardError()).trimmed();
        if (!diagnostic.isEmpty())
            qWarning().noquote() << "KWS bridge stderr:" << diagnostic.left(800);
    });
    connect(&m_process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus status) {
        m_stopTimer.stop();
        m_startupTimer.stop();
        killProcessTree();
        m_processGroup = 0;
        m_ready = false;
        m_paused = false;
        if (m_stopping || !m_started) {
            m_stopping = false;
            emit kwsStopped();
            return;
        }
        scheduleRestart(QStringLiteral("bridge exited code=%1 status=%2")
                            .arg(exitCode).arg(static_cast<int>(status)));
    });
    connect(&m_process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_startupTimer.stop();
            scheduleRestart(m_process.errorString());
        }
    });

    m_restartTimer.setSingleShot(true);
    m_restartTimer.setInterval(qMax(250, configuration.restartDelayMs));
    connect(&m_restartTimer, &QTimer::timeout,
            this, &KwsProcessAdapter::startProcess);
    m_stopTimer.setSingleShot(true);
    m_stopTimer.setInterval(4'000);
    connect(&m_stopTimer, &QTimer::timeout, this, [this] {
        if (m_process.state() != QProcess::NotRunning)
            killProcessTree();
    });
    m_startupTimer.setSingleShot(true);
    m_startupTimer.setInterval(60'000);
    connect(&m_startupTimer, &QTimer::timeout, this, [this] {
        emit kwsError(QStringLiteral("本地语音唤醒启动超时"), QStringLiteral("startup_timeout"));
        killProcessTree();
    });
}

void KwsProcessAdapter::killProcessTree()
{
#ifdef Q_OS_LINUX
    // Only the session/process group created by this adapter; never a shell name search.
    if (m_processGroup > 1)
        ::kill(-static_cast<pid_t>(m_processGroup), SIGKILL);
#endif
    if (m_process.state() != QProcess::NotRunning)
        m_process.kill();
}

KwsProcessAdapter::~KwsProcessAdapter()
{
    m_started = false;
    m_restartTimer.stop();
    if (m_process.state() != QProcess::NotRunning) {
        killProcessTree();
        m_process.waitForFinished(500);
    }
}

void KwsProcessAdapter::start()
{
    if (!m_configuration.enabled || m_started)
        return;
    m_started = true;
    startProcess();
}

void KwsProcessAdapter::pause()
{
    m_pauseRequested = true;
    if (m_process.state() == QProcess::NotRunning) {
        m_paused = true;
        emit kwsPaused();
        return;
    }
    // Loading is still a live process; never synthesize a release acknowledgement.
    if (!m_ready)
        return;
    if (m_paused) {
        emit kwsPaused();
        return;
    }
    sendCommand(QStringLiteral("pause"));
}

void KwsProcessAdapter::resume()
{
    m_pauseRequested = false;
    if (!m_started)
        return;
    if (m_process.state() == QProcess::NotRunning) {
        startProcess();
        return;
    }
    if (!m_ready || !m_paused)
        return;
    m_paused = false; // A resume in flight is NOT a released microphone.
    sendCommand(QStringLiteral("resume"));
}

void KwsProcessAdapter::stop()
{
    m_started = false;
    m_pauseRequested = true;
    m_stopping = true;
    m_restartTimer.stop();
    if (m_process.state() == QProcess::NotRunning) {
        m_stopping = false;
        emit kwsStopped();
        return;
    }
    sendCommand(QStringLiteral("stop"));
    m_stopTimer.start();
}

bool KwsProcessAdapter::isRunning() const
{
    return m_process.state() != QProcess::NotRunning;
}

bool KwsProcessAdapter::isPaused() const
{
    return m_paused;
}

void KwsProcessAdapter::startProcess()
{
    if (!m_started || m_process.state() != QProcess::NotRunning)
        return;
    qInfo().noquote()
        << QStringLiteral(
               "KWS resolved config: bridge=%1 backend=%2 alsa_device=%3 input_device=%4")
               .arg(m_configuration.bridgeScript, m_configuration.captureBackend,
                    m_configuration.alsaDevice.isEmpty()
                        ? QStringLiteral("<empty>")
                        : m_configuration.alsaDevice,
                    m_configuration.inputDevice.isEmpty()
                        ? QStringLiteral("<empty>")
                        : m_configuration.inputDevice);
    const QString configurationError = m_configuration.validationError();
    if (!configurationError.isEmpty()) {
        emit kwsError(QStringLiteral("本地语音唤醒配置不完整"), configurationError);
        return;
    }
    for (const QString& path : {m_configuration.bridgeScript,
                                m_configuration.kwsRoot,
                                m_configuration.modelPath,
                                m_configuration.tokensPath}) {
        if (!QFileInfo::exists(path)) {
            emit kwsError(QStringLiteral("本地语音唤醒文件缺失"),
                          QStringLiteral("missing path: %1").arg(path));
            return;
        }
    }

    QStringList arguments {
        m_configuration.bridgeScript,
        QStringLiteral("--start-paused"),
        QStringLiteral("--kws-root"), m_configuration.kwsRoot,
        QStringLiteral("--model"), m_configuration.modelPath,
        QStringLiteral("--tokens"), m_configuration.tokensPath,
        QStringLiteral("--capture-backend"), m_configuration.captureBackend,
        QStringLiteral("--input-samplerate"),
        QString::number(m_configuration.inputSampleRate),
        QStringLiteral("--wake-threshold"), number(m_configuration.wakeThreshold),
        QStringLiteral("--nihao-threshold"),
        number(m_configuration.ignoredHelloThreshold),
        QStringLiteral("--peiwoshuohua-threshold"),
        number(m_configuration.companionThreshold),
        QStringLiteral("--jiuming-threshold"),
        number(m_configuration.emergencyThreshold),
        QStringLiteral("--command-threshold"), number(m_configuration.commandThreshold),
        QStringLiteral("--vad-threshold-db"),
        number(m_configuration.vadThresholdDb),
        QStringLiteral("--vad-noise-ratio"),
        number(m_configuration.vadNoiseRatio)
    };
    if (!m_configuration.inputDevice.trimmed().isEmpty())
        arguments.append({QStringLiteral("--device"), m_configuration.inputDevice});
    if (!m_configuration.alsaDevice.trimmed().isEmpty())
        arguments.append({QStringLiteral("--alsa-device"), m_configuration.alsaDevice});

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    environment.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    m_process.setProcessEnvironment(environment);
    m_process.setProgram(m_configuration.pythonProgram);
    m_process.setArguments(arguments);
    m_stdoutBuffer.clear();
    m_waitingCommandId = 0;
    m_ready = false;
    m_paused = false;
    m_stopping = false;
    m_process.start();
    m_startupTimer.start();
}

void KwsProcessAdapter::sendCommand(const QString& command)
{
    if (m_process.state() == QProcess::NotRunning)
        return;
    const QJsonObject object {
        {QStringLiteral("protocol"), QStringLiteral("longpet-kws")},
        {QStringLiteral("version"), KwsProtocolVersion},
        {QStringLiteral("command_id"), ++m_commandSequence},
        {QStringLiteral("command"), command}
    };
    m_waitingCommandId = m_commandSequence;
    m_process.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    m_process.write("\n");
}

void KwsProcessAdapter::consumeOutput()
{
    m_stdoutBuffer.append(m_process.readAllStandardOutput());
    if (m_stdoutBuffer.size() > 64 * 1024) {
        m_stdoutBuffer.clear();
        emit kwsError(QStringLiteral("本地语音唤醒输出异常"), QStringLiteral("JSONL buffer exceeded limit"));
        killProcessTree();
        return;
    }
    while (true) {
        const qsizetype newline = m_stdoutBuffer.indexOf('\n');
        if (newline < 0)
            break;
        const QByteArray line = m_stdoutBuffer.left(newline).trimmed();
        m_stdoutBuffer.remove(0, newline + 1);
        if (!line.isEmpty())
            processLine(line);
    }
}

void KwsProcessAdapter::processLine(const QByteArray& line)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit kwsError(QStringLiteral("本地语音唤醒输出异常"),
                      QStringLiteral("invalid JSONL: %1").arg(
                          QString::fromUtf8(line.left(240))));
        return;
    }
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("protocol")).toString()
            != QStringLiteral("longpet-kws")
        || object.value(QStringLiteral("version")).toInt()
            != KwsProtocolVersion) {
        emit kwsError(QStringLiteral("本地语音唤醒协议不兼容"),
                      QStringLiteral("unsupported protocol/version"));
        return;
    }
    const QString event = object.value(QStringLiteral("event")).toString();
    if (event == QStringLiteral("ready")) {
        m_startupTimer.stop();
        m_ready = true;
        m_paused = object.value(QStringLiteral("paused")).toBool();
        if (!m_paused) {
            emit kwsError(QStringLiteral("请更新本地唤醒脚本"), QStringLiteral("bridge must start paused"));
            killProcessTree();
            return;
        }
        m_failureReported = false;
        qInfo().noquote() << QStringLiteral("KWS ready startup_ms=%1 model_load_ms=%2 paused=1")
            .arg(object.value(QStringLiteral("startup_ms")).toInteger(-1))
            .arg(object.value(QStringLiteral("model_load_ms")).toInteger(-1));
        emit kwsReady();
        return;
    }
    if (event == QStringLiteral("keyword")) {
        if (m_ready && !m_paused && !m_pauseRequested && !m_stopping) {
            emit keywordDetected({object.value(QStringLiteral("keyword")).toString(),
                                  object.value(QStringLiteral("score")).toDouble(),
                                  object.value(QStringLiteral("timestamp_ms")).toInteger()});
        }
        return;
    }
    if (event == QStringLiteral("paused")) {
        if (!m_pauseRequested || object.value(QStringLiteral("command_id")).toInteger()
                != m_waitingCommandId)
            return;
        m_paused = true;
        qInfo().noquote() << QStringLiteral("KWS paused command_id=%1 release_ms=%2")
            .arg(m_waitingCommandId).arg(object.value(QStringLiteral("release_ms")).toInteger(-1));
        emit kwsPaused();
        return;
    }
    if (event == QStringLiteral("resumed")) {
        if (object.value(QStringLiteral("command_id")).toInteger() != m_waitingCommandId)
            return;
        m_paused = false;
        if (m_pauseRequested) {
            sendCommand(QStringLiteral("pause"));
            return;
        }
        emit kwsResumed();
        qInfo().noquote() << QStringLiteral("KWS resumed command_id=%1").arg(m_waitingCommandId);
        return;
    }
    if (event == QStringLiteral("error")) {
        m_failureReported = true;
        emit kwsError(QStringLiteral("本地语音唤醒暂时不可用"),
                      object.value(QStringLiteral("message")).toString());
        return;
    }
    if (event == QStringLiteral("stopped")) {
        m_ready = false;
        // Wait for QProcess::finished, not an unverified stdout claim.
    }
}

void KwsProcessAdapter::scheduleRestart(const QString& diagnostic)
{
    if (!m_failureReported) {
        m_failureReported = true;
        emit kwsError(QStringLiteral("本地语音唤醒已中断，正在恢复"), diagnostic);
    }
    if (m_started && !m_restartTimer.isActive())
        m_restartTimer.start();
}
