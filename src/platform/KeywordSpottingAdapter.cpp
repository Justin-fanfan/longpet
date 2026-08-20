#include "KeywordSpottingAdapter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QDebug>

#include <utility>

#ifdef Q_OS_LINUX
#include <signal.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace {
constexpr qsizetype MaximumBufferedOutput = 256 * 1024;
QString statusSummary(const QJsonObject& object, const QString& fallback)
{
    const QString detail = object.value(QStringLiteral("detail")).toString().simplified();
    return detail.isEmpty() ? fallback : detail;
}

bool environmentFlag(const QByteArray& name, bool fallback)
{
    if (!qEnvironmentVariableIsSet(name.constData()))
        return fallback;
    const QString value = qEnvironmentVariable(name.constData()).trimmed().toLower();
    return value != QStringLiteral("0") && value != QStringLiteral("false")
        && value != QStringLiteral("off") && value != QStringLiteral("no");
}

double environmentDouble(const QByteArray& name, double fallback)
{
    bool ok = false;
    const double value = qEnvironmentVariable(name.constData()).toDouble(&ok);
    return ok ? value : fallback;
}
}

KeywordSpottingAdapter::KeywordSpottingAdapter(QObject* parent)
    : KeywordSpottingAdapter(defaultOptions(), parent)
{
}

KeywordSpottingAdapter::KeywordSpottingAdapter(const Options& options,
                                               QObject* parent)
    : QObject(parent),
      m_options(options)
{
    m_startupTimer.setSingleShot(true);
    m_killTimer.setSingleShot(true);
    m_retryTimer.setSingleShot(true);
    m_stabilityTimer.setSingleShot(true);
    m_stabilityTimer.setInterval(5 * 60 * 1000);
    updateStatusConfiguration();
    m_startupTimer.setInterval(qMax(1, m_options.startupTimeoutMs));
    connect(&m_startupTimer, &QTimer::timeout, this, [this] {
        if (m_status.state != KeywordSpottingRuntimeState::Starting)
            return;
        publishStatus(KeywordSpottingRuntimeState::Error, false, false,
                      QStringLiteral("关键词模型启动超时"));
        m_process.terminate();
        m_killTimer.start(qMax(100, m_options.killFallbackMs));
    });
    connect(&m_killTimer, &QTimer::timeout,
            this, &KeywordSpottingAdapter::forceKill);
    connect(&m_retryTimer, &QTimer::timeout, this, [this] {
        if (m_options.enabled && m_process.state() == QProcess::NotRunning)
            start();
    });
    connect(&m_stabilityTimer, &QTimer::timeout, this,
            [this] { m_retryAttempt = 0; });
    connect(&m_process, &QProcess::readyReadStandardOutput,
            this, &KeywordSpottingAdapter::readStandardOutput);
    connect(&m_process, &QProcess::readyReadStandardError,
            this, &KeywordSpottingAdapter::readStandardError);
    connect(&m_process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
        if (m_stopping || error == QProcess::Crashed)
            return;
        m_nonRecoverableFailure = error == QProcess::FailedToStart;
        publishStatus(KeywordSpottingRuntimeState::Error, false, false,
                      QStringLiteral("无法启动关键词识别进程：%1")
                          .arg(m_process.errorString()));
    });
    connect(&m_process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &KeywordSpottingAdapter::handleFinished);
    connect(&m_process, &QProcess::started, this, [this] {
        m_processGroupId = m_process.processId();
        m_status.workerPid = m_process.processId();
        m_status.startedAt = QDateTime::currentDateTime();
        m_status.running = true;
        emit statusChanged(m_status);
    });
#ifdef Q_OS_LINUX
    m_process.setChildProcessModifier([] {
        const pid_t expectedParent = getppid();
        setpgid(0, 0);
        setpriority(PRIO_PROCESS, 0, 5);
        if (prctl(PR_SET_PDEATHSIG, SIGTERM) != 0)
            return;
        if (getppid() != expectedParent)
            kill(getpid(), SIGTERM);
    });
#endif
}

KeywordSpottingAdapter::~KeywordSpottingAdapter()
{
    m_startupTimer.stop();
    m_retryTimer.stop();
    m_stabilityTimer.stop();
    m_killTimer.stop();
    if (m_process.state() != QProcess::NotRunning) {
        forceKill();
        // Destruction is the final safety net after the normal asynchronous
        // stop path. A short reap avoids leaving a QProcess/zombie behind;
        // normal UI operations never wait here.
        m_process.waitForFinished(100);
    }
}

bool KeywordSpottingAdapter::start()
{
    m_retryTimer.stop();
    if (m_process.state() != QProcess::NotRunning)
        return true;
    if (!m_options.enabled) {
        publishStatus(KeywordSpottingRuntimeState::Disabled, false, false,
                      QStringLiteral("关键词识别未启用"));
        return false;
    }

    QString error;
    if (!validateRuntime(&error)) {
        m_nonRecoverableFailure = true;
        publishStatus(KeywordSpottingRuntimeState::Error, false, false, error);
        return false;
    }

    const QDir root(m_options.runtimeRoot);
    const QString script = root.filePath(QStringLiteral("src/loongson_kws.py"));
    const QString keywords = root.filePath(QStringLiteral("config/keywords.txt"));
    const QStringList arguments {
        script,
        QStringLiteral("--keywords"), keywords,
        QStringLiteral("--threshold"), QString::number(m_options.threshold, 'f', 3),
        QStringLiteral("--score"), QString::number(m_options.score, 'f', 3),
        QStringLiteral("--alsa-device"), m_options.audioDevice,
        QStringLiteral("--sample-rate"), QString::number(m_options.captureSampleRate),
        QStringLiteral("--channels"), QString::number(m_options.captureChannels),
        QStringLiteral("--mic-channel"), QString::number(m_options.microphoneChannel)
    };
    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();
    m_stopping = false;
    m_nonRecoverableFailure = false;
    updateStatusConfiguration();
    m_process.setWorkingDirectory(root.absolutePath());
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_process.setProgram(m_options.pythonExecutable);
    m_process.setArguments(arguments);
    publishStatus(KeywordSpottingRuntimeState::Starting, false, false,
                  QStringLiteral("关键词模型加载中"));
    m_startupTimer.start();
    m_process.start();
    return true;
}

void KeywordSpottingAdapter::stop()
{
    m_retryTimer.stop();
    if (m_process.state() != QProcess::NotRunning)
        requestStop(false);
    else {
        publishStatus(KeywordSpottingRuntimeState::Disabled, false, false,
                      QStringLiteral("关键词识别已停止"));
    }
}

bool KeywordSpottingAdapter::restart()
{
    m_retryAttempt = 0;
    m_retryTimer.stop();
    if (!m_options.enabled)
        m_options.enabled = true;
    if (m_process.state() != QProcess::NotRunning) {
        requestStop(true);
        return true;
    }
    return start();
}

bool KeywordSpottingAdapter::setEnabled(bool enabled)
{
    m_options.enabled = enabled;
    updateStatusConfiguration();
    emit statusChanged(m_status);
    if (!enabled) {
        stop();
        return true;
    }
    return restart();
}

bool KeywordSpottingAdapter::reconfigure(const Options& options, QString* error)
{
    if (options.captureSampleRate < 8'000 || options.captureSampleRate > 192'000
        || options.captureChannels < 1 || options.captureChannels > 8
        || options.microphoneChannel < 0
        || options.microphoneChannel >= options.captureChannels
        || options.audioDevice.trimmed().isEmpty()
        || options.threshold <= 0.0 || options.threshold > 1.0
        || options.score <= 0.0 || options.score > 10.0) {
        if (error)
            *error = QStringLiteral("KWS 输入或模型参数无效");
        return false;
    }
    m_options = options;
    m_startupTimer.setInterval(qMax(1, m_options.startupTimeoutMs));
    updateStatusConfiguration();
    emit statusChanged(m_status);
    m_retryAttempt = 0;
    m_retryTimer.stop();
    if (m_process.state() != QProcess::NotRunning) {
        requestStop(m_options.enabled);
        return true;
    }
    return !m_options.enabled || start();
}

bool KeywordSpottingAdapter::isRunning() const
{
    return m_process.state() != QProcess::NotRunning;
}

KeywordSpottingStatus KeywordSpottingAdapter::status() const
{
    return m_status;
}

KeywordSpottingAdapter::Options KeywordSpottingAdapter::options() const
{
    return m_options;
}

KeywordSpottingAdapter::Options KeywordSpottingAdapter::defaultOptions()
{
    Options options;
#ifdef Q_OS_LINUX
    options.enabled = true;
#endif
    options.enabled = environmentFlag(QByteArrayLiteral("LONGPET_KWS_ENABLED"),
                                      options.enabled);
    options.pythonExecutable = qEnvironmentVariable("LONGPET_KWS_PYTHON",
                                                     QStringLiteral("python3"));
    options.threshold = environmentDouble(QByteArrayLiteral("LONGPET_KWS_THRESHOLD"),
                                          options.threshold);
    options.score = environmentDouble(QByteArrayLiteral("LONGPET_KWS_SCORE"),
                                      options.score);
    options.audioDevice = qEnvironmentVariable(
        "LONGPET_KWS_ALSA_DEVICE", options.audioDevice);
    options.runtimeRoot = qEnvironmentVariable("LONGPET_KWS_ROOT");
    if (!options.runtimeRoot.isEmpty())
        return options;

    const QDir applicationDirectory(QCoreApplication::applicationDirPath());
    const QString adjacent = applicationDirectory.filePath(QStringLiteral("kws"));
    const QString installed = QDir(applicationDirectory.filePath(QStringLiteral("../share/longpet")))
        .filePath(QStringLiteral("kws"));
    options.runtimeRoot = QFileInfo::exists(adjacent) ? adjacent : installed;
    return options;
}

bool KeywordSpottingAdapter::parseKeywordEvent(const QByteArray& line,
                                               KeywordDetection* detection)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(line.trimmed(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return false;
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("event")).toString()
        != QStringLiteral("keyword_detected")) {
        return false;
    }
    KeywordDetection parsed;
    parsed.keyword = object.value(QStringLiteral("keyword")).toString().trimmed();
    if (parsed.keyword.isEmpty())
        return false;
    parsed.signal = object.value(QStringLiteral("signal")).toString().trimmed();
    parsed.code = object.value(QStringLiteral("code")).toInt();
    parsed.source = object.value(QStringLiteral("source")).toString().trimmed();
    parsed.timestamp = QDateTime::fromString(
        object.value(QStringLiteral("timestamp")).toString(), Qt::ISODateWithMs);
    if (!parsed.timestamp.isValid())
        parsed.timestamp = QDateTime::currentDateTime();
    if (detection)
        *detection = parsed;
    return true;
}

bool KeywordSpottingAdapter::parseRuntimeStatusEvent(
    const QByteArray& line, KeywordSpottingStatus* status)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(line.trimmed(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return false;
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("event")).toString()
        != QStringLiteral("runtime_status")) {
        return false;
    }

    KeywordSpottingStatus parsed;
    const QString state = object.value(QStringLiteral("state")).toString().toLower();
    if (state == QStringLiteral("listening")) {
        parsed.state = KeywordSpottingRuntimeState::Listening;
        parsed.available = true;
        parsed.listening = true;
        parsed.summary = statusSummary(object, QStringLiteral("离线关键词 · 正在监听"));
    } else if (state == QStringLiteral("starting")) {
        parsed.state = KeywordSpottingRuntimeState::Starting;
        parsed.summary = statusSummary(object, QStringLiteral("关键词模型加载中"));
    } else if (state == QStringLiteral("error")) {
        parsed.state = KeywordSpottingRuntimeState::Error;
        parsed.summary = statusSummary(object, QStringLiteral("关键词识别不可用"));
    } else if (state == QStringLiteral("stopped")
               || state == QStringLiteral("stopping")) {
        parsed.state = KeywordSpottingRuntimeState::Disabled;
        parsed.summary = statusSummary(object, QStringLiteral("关键词识别已停止"));
    } else {
        return false;
    }
    if (status)
        *status = parsed;
    return true;
}

void KeywordSpottingAdapter::readStandardOutput()
{
    m_stdoutBuffer += m_process.readAllStandardOutput();
    if (m_stdoutBuffer.size() > MaximumBufferedOutput
        && !m_stdoutBuffer.contains('\n')) {
        m_stdoutBuffer.clear();
        emit diagnosticMessage(QStringLiteral("KWS 进程输出行超过 256 KiB，已丢弃"));
        return;
    }
    qsizetype newline = -1;
    while ((newline = m_stdoutBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_stdoutBuffer.left(newline).trimmed();
        m_stdoutBuffer.remove(0, newline + 1);
        if (line.isEmpty())
            continue;
        KeywordDetection detection;
        if (parseKeywordEvent(line, &detection)) {
            emit keywordDetected(detection);
            continue;
        }
        QJsonParseError jsonError;
        const QJsonDocument eventDocument = QJsonDocument::fromJson(line, &jsonError);
        if (jsonError.error == QJsonParseError::NoError && eventDocument.isObject()) {
            const QJsonObject event = eventDocument.object();
            if (event.value(QStringLiteral("event")).toString()
                == QStringLiteral("audio_level")) {
                m_status.inputRms = qBound(0.0,
                    event.value(QStringLiteral("rms")).toDouble(), 1.0);
                m_status.inputPeak = qBound(0.0,
                    event.value(QStringLiteral("peak")).toDouble(), 1.0);
                m_status.droppedUtterances = event.value(
                    QStringLiteral("dropped_utterances")).toInt(
                        m_status.droppedUtterances);
                emit statusChanged(m_status);
                continue;
            }
            if (event.value(QStringLiteral("event")).toString()
                == QStringLiteral("decode_metrics")) {
                m_status.lastDecodeElapsedMs = qMax(0.0,
                    event.value(QStringLiteral("elapsed_ms")).toDouble());
                m_status.lastRtf = qMax(0.0,
                    event.value(QStringLiteral("rtf")).toDouble());
                m_status.lastKeywordLatencyMs = qMax(0.0,
                    event.value(QStringLiteral("keyword_latency_ms")).toDouble());
                m_status.droppedUtterances = event.value(
                    QStringLiteral("dropped_utterances")).toInt(
                        m_status.droppedUtterances);
                emit statusChanged(m_status);
                continue;
            }
        }
        KeywordSpottingStatus runtimeStatus;
        if (parseRuntimeStatusEvent(line, &runtimeStatus)) {
            if (eventDocument.isObject()) {
                m_nonRecoverableFailure = !eventDocument.object().value(
                    QStringLiteral("recoverable")).toBool(true);
            }
            if (runtimeStatus.state == KeywordSpottingRuntimeState::Listening) {
                m_startupTimer.stop();
                m_stabilityTimer.start();
            }
            publishStatus(runtimeStatus.state, runtimeStatus.available,
                          runtimeStatus.listening, runtimeStatus.summary);
            continue;
        }
        emit diagnosticMessage(QStringLiteral("忽略无法解析的 KWS 输出：%1")
                                   .arg(QString::fromUtf8(line)));
    }
}

void KeywordSpottingAdapter::readStandardError()
{
    m_stderrBuffer += m_process.readAllStandardError();
    if (m_stderrBuffer.size() > 8'192)
        m_stderrBuffer = m_stderrBuffer.right(8'192);
    qsizetype newline = -1;
    while ((newline = m_stderrBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_stderrBuffer.left(newline).trimmed();
        m_stderrBuffer.remove(0, newline + 1);
        if (!line.isEmpty())
            emit diagnosticMessage(QString::fromUtf8(line));
    }
}

void KeywordSpottingAdapter::handleFinished(int exitCode,
                                            QProcess::ExitStatus exitStatus)
{
    m_startupTimer.stop();
    m_killTimer.stop();
    m_stabilityTimer.stop();
#ifdef Q_OS_LINUX
    if (m_processGroupId > 0)
        ::kill(-static_cast<pid_t>(m_processGroupId), SIGTERM);
#endif
    m_status.running = false;
    m_status.workerPid = 0;
    m_processGroupId = 0;
    if (m_stopping) {
        const bool shouldRestart = m_restartAfterStop;
        m_stopping = false;
        m_restartAfterStop = false;
        m_stdoutBuffer.clear();
        m_stderrBuffer.clear();
        publishStatus(KeywordSpottingRuntimeState::Disabled, false, false,
                      QStringLiteral("关键词识别已停止"));
        if (shouldRestart)
            QTimer::singleShot(0, this, [this] { start(); });
        return;
    }
    const QString reason = exitStatus == QProcess::CrashExit
        ? QStringLiteral("关键词识别进程异常退出")
        : QStringLiteral("关键词识别进程已退出（%1）").arg(exitCode);
    scheduleRecovery(reason);
}

void KeywordSpottingAdapter::requestStop(bool restartAfterStop)
{
    m_startupTimer.stop();
    m_retryTimer.stop();
    m_stopping = true;
    m_restartAfterStop = restartAfterStop;
#ifdef Q_OS_LINUX
    if (m_processGroupId > 0)
        ::kill(-static_cast<pid_t>(m_processGroupId), SIGTERM);
#endif
    m_process.terminate();
    m_killTimer.start(qMax(100, m_options.killFallbackMs));
}

void KeywordSpottingAdapter::forceKill()
{
    if (m_process.state() == QProcess::NotRunning)
        return;
    emit forceKillInvoked();
#ifdef Q_OS_LINUX
    if (m_processGroupId > 0)
        ::kill(-static_cast<pid_t>(m_processGroupId), SIGKILL);
#endif
    m_process.kill();
}

void KeywordSpottingAdapter::scheduleRecovery(const QString& reason)
{
    if (!m_options.enabled || m_nonRecoverableFailure
        || m_retryAttempt >= m_options.retryDelaysMs.size()) {
        publishStatus(m_nonRecoverableFailure
                          ? KeywordSpottingRuntimeState::Error
                          : KeywordSpottingRuntimeState::Degraded,
                      false, false,
                      m_nonRecoverableFailure ? reason
                          : QStringLiteral("%1；自动恢复次数已用尽").arg(reason));
        return;
    }
    const int delay = qMax(1, m_options.retryDelaysMs.at(m_retryAttempt));
    ++m_retryAttempt;
    publishStatus(KeywordSpottingRuntimeState::Degraded, false, false,
                  QStringLiteral("%1；%2 秒后第 %3 次重试")
                      .arg(reason).arg(delay / 1000.0, 0, 'f', 1)
                      .arg(m_retryAttempt));
    emit recoveryScheduled(m_retryAttempt, delay);
    m_retryTimer.start(delay);
}

void KeywordSpottingAdapter::updateStatusConfiguration()
{
    m_status.enabled = m_options.enabled;
    m_status.audioDevice = m_options.audioDevice;
    m_status.captureSampleRate = m_options.captureSampleRate;
    m_status.captureChannels = m_options.captureChannels;
    m_status.microphoneChannel = m_options.microphoneChannel;
    m_status.threshold = m_options.threshold;
    m_status.score = m_options.score;
}

void KeywordSpottingAdapter::publishStatus(
    KeywordSpottingRuntimeState state, bool available, bool listening,
    const QString& summary)
{
    updateStatusConfiguration();
    KeywordSpottingStatus next = m_status;
    next.state = state;
    next.enabled = m_options.enabled;
    next.available = available;
    next.running = m_process.state() != QProcess::NotRunning;
    next.listening = listening;
    next.summary = summary.simplified();
    next.errorDetail = (state == KeywordSpottingRuntimeState::Error
                        || state == KeywordSpottingRuntimeState::Degraded)
        ? next.summary : QString();
    if (next.state == m_status.state && next.enabled == m_status.enabled
        && next.running == m_status.running
        && next.available == m_status.available
        && next.listening == m_status.listening
        && next.summary == m_status.summary
        && next.errorDetail == m_status.errorDetail) {
        return;
    }
    m_status = next;
    emit statusChanged(m_status);
}

bool KeywordSpottingAdapter::validateRuntime(QString* error) const
{
    if (m_options.audioDevice.trimmed().isEmpty()
        || m_options.captureSampleRate < 8'000
        || m_options.captureSampleRate > 192'000
        || m_options.captureChannels < 1 || m_options.captureChannels > 8
        || m_options.microphoneChannel < 0
        || m_options.microphoneChannel >= m_options.captureChannels
        || m_options.threshold <= 0.0 || m_options.threshold > 1.0
        || m_options.score <= 0.0 || m_options.score > 10.0) {
        if (error)
            *error = QStringLiteral("KWS 配置无效，拒绝启动 worker");
        return false;
    }
    const QFileInfo executable(m_options.pythonExecutable);
    if ((executable.isAbsolute() && !executable.isExecutable())
        || (!executable.isAbsolute()
            && QStandardPaths::findExecutable(m_options.pythonExecutable).isEmpty())) {
        if (error)
            *error = QStringLiteral("KWS Python 可执行文件不可用：%1")
                         .arg(m_options.pythonExecutable);
        return false;
    }
    const QDir root(m_options.runtimeRoot);
    const QString model = QStringLiteral(
        "models/sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01/");
    const QStringList required {
        QStringLiteral("src/loongson_kws.py"),
        QStringLiteral("config/keywords.txt"),
        model + QStringLiteral("encoder-epoch-12-avg-2-chunk-16-left-64.int8.onnx"),
        model + QStringLiteral("decoder-epoch-12-avg-2-chunk-16-left-64.int8.onnx"),
        model + QStringLiteral("joiner-epoch-12-avg-2-chunk-16-left-64.int8.onnx"),
        model + QStringLiteral("tokens.txt")
    };
    for (const QString& relativePath : required) {
        if (!QFileInfo::exists(root.filePath(relativePath))) {
            if (error) {
                *error = QStringLiteral("关键词运行时不完整：缺少 %1")
                    .arg(relativePath);
            }
            return false;
        }
    }
    return true;
}
