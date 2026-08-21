#include "VisionAdapter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QProcessEnvironment>
#include <QStandardPaths>

#include <cmath>

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
    const QString detail = object.value(QStringLiteral("detail"))
                               .toString()
                               .simplified();
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

int environmentInt(const QByteArray& name, int fallback, int minimum, int maximum)
{
    bool ok = false;
    const int value = qEnvironmentVariable(name.constData()).toInt(&ok);
    return ok ? qBound(minimum, value, maximum) : fallback;
}

bool jsonBool(const QJsonObject& object, const QString& name, bool fallback)
{
    const QJsonValue value = object.value(name);
    return value.isBool() ? value.toBool() : fallback;
}

double nonNegativeMetric(const QJsonObject& object, const QString& name)
{
    const QJsonValue value = object.value(name);
    if (!value.isDouble())
        return 0.0;
    const double metric = value.toDouble();
    return std::isfinite(metric) && metric >= 0.0 ? metric : 0.0;
}

VisionEventType eventTypeFor(QString type)
{
    type = type.trimmed().toLower();
    type.replace(QLatin1Char('-'), QLatin1Char('_'));
    if (type == QStringLiteral("person")
        || type == QStringLiteral("person_detected")
        || type == QStringLiteral("person_present")) {
        return VisionEventType::PersonDetected;
    }
    if (type == QStringLiteral("fall_candidate")
        || type == QStringLiteral("fall_suspected")) {
        return VisionEventType::FallCandidate;
    }
    if (type == QStringLiteral("fall")
        || type == QStringLiteral("fall_detected")
        || type == QStringLiteral("fall_confirmed")) {
        return VisionEventType::FallConfirmed;
    }
    if (type == QStringLiteral("wave")
        || type == QStringLiteral("wave_detected")
        || type == QStringLiteral("hand_wave")) {
        return VisionEventType::Wave;
    }
    return VisionEventType::Unknown;
}

QDateTime parseTimestamp(const QJsonValue& value)
{
    if (value.isString()) {
        QDateTime timestamp = QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
        if (!timestamp.isValid())
            timestamp = QDateTime::fromString(value.toString(), Qt::ISODate);
        if (timestamp.isValid())
            return timestamp;
    } else if (value.isDouble()) {
        const double raw = value.toDouble();
        if (std::isfinite(raw)) {
            if (raw >= 100'000'000'000.0)
                return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(raw));
            if (raw >= 1'000'000'000.0)
                return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(raw));
        }
    }
    // Monotonic timestamps cannot be converted to wall-clock time here. Use the
    // receipt time instead; workers may retain the original value in metadata.
    return QDateTime::currentDateTime();
}

bool sameStatus(const VisionStatus& first, const VisionStatus& second)
{
    return first.state == second.state && first.available == second.available
        && first.cameraAvailable == second.cameraAvailable
        && first.monitoring == second.monitoring
        && first.summary == second.summary
        && qFuzzyCompare(first.effectiveFps + 1.0, second.effectiveFps + 1.0)
        && qFuzzyCompare(first.frameTimeMs + 1.0, second.frameTimeMs + 1.0)
        && first.cameraIndex == second.cameraIndex
        && first.lastEventType == second.lastEventType
        && first.lastEventAt == second.lastEventAt;
}
}

VisionAdapter::VisionAdapter(QObject* parent)
    : VisionAdapter(defaultOptions(), parent)
{
}

VisionAdapter::VisionAdapter(const Options& options, QObject* parent)
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
        if (m_status.state != VisionRuntimeState::Starting)
            return;
        publishStatus(VisionRuntimeState::Degraded, false, false, false,
                      QStringLiteral("视觉进程启动超时"), 0.0, 0.0,
                      m_options.cameraIndex);
        m_process.terminate();
        m_killTimer.start(qMax(100, m_options.killFallbackMs));
    });
    connect(&m_killTimer, &QTimer::timeout,
            this, &VisionAdapter::forceKill);
    connect(&m_retryTimer, &QTimer::timeout, this, [this] {
        if (m_options.enabled && m_process.state() == QProcess::NotRunning)
            start();
    });
    connect(&m_stabilityTimer, &QTimer::timeout, this,
            [this] { m_retryAttempt = 0; });
    connect(&m_process, &QProcess::readyReadStandardOutput,
            this, &VisionAdapter::readStandardOutput);
    connect(&m_process, &QProcess::readyReadStandardError,
            this, &VisionAdapter::readStandardError);
    connect(&m_process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
        if (m_stopping || error == QProcess::Crashed)
            return;
        m_startupTimer.stop();
        m_nonRecoverableFailure = error == QProcess::FailedToStart;
        publishStatus(VisionRuntimeState::Degraded, false, false, false,
                      QStringLiteral("无法启动视觉进程：%1")
                          .arg(m_process.errorString()),
                      0.0, 0.0, m_options.cameraIndex);
    });
    connect(&m_process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &VisionAdapter::handleFinished);
    connect(&m_process, &QProcess::started, this, [this] {
        m_processGroupId = m_process.processId();
        m_status.workerPid = m_process.processId();
        m_status.startedAt = QDateTime::currentDateTime();
        m_status.running = true;
        emit statusChanged(m_status);
    });
#ifdef Q_OS_LINUX
    const int niceAdjustment = qBound(0, m_options.niceAdjustment, 19);
    m_process.setChildProcessModifier([niceAdjustment] {
        const pid_t expectedParent = getppid();
        setpgid(0, 0);
        setpriority(PRIO_PROCESS, 0, niceAdjustment);
        if (prctl(PR_SET_PDEATHSIG, SIGTERM) != 0)
            return;
        if (getppid() != expectedParent)
            kill(getpid(), SIGTERM);
    });
#endif
}

VisionAdapter::~VisionAdapter()
{
    m_startupTimer.stop();
    m_retryTimer.stop();
    m_stabilityTimer.stop();
    m_killTimer.stop();
    if (m_process.state() != QProcess::NotRunning) {
        forceKill();
        m_process.waitForFinished(100);
    }
}

bool VisionAdapter::start()
{
    m_retryTimer.stop();
    if (m_process.state() != QProcess::NotRunning)
        return true;
    if (!m_options.enabled) {
        publishStatus(VisionRuntimeState::Disabled, false, false, false,
                      QStringLiteral("视觉感知未启用"));
        return false;
    }

    QString error;
    if (!validateRuntime(&error)) {
        m_nonRecoverableFailure = true;
        publishStatus(VisionRuntimeState::Degraded, false, false, false, error,
                      0.0, 0.0, m_options.cameraIndex);
        return false;
    }

    const QDir root(m_options.runtimeRoot);
    const QString script = root.filePath(m_options.workerScript);
    QStringList arguments {
        script,
        QStringLiteral("--camera"), QString::number(m_options.cameraIndex),
        QStringLiteral("--width"), QString::number(qMax(1, m_options.frameWidth)),
        QStringLiteral("--height"), QString::number(qMax(1, m_options.frameHeight)),
        QStringLiteral("--fps"), QString::number(qMax(1, m_options.targetFps))
    };
    if (m_options.fallEnabled)
        arguments.append(QStringLiteral("--fall-enabled"));
    if (m_options.waveEnabled)
        arguments.append(QStringLiteral("--wave-enabled"));
    arguments.append(m_options.additionalArguments);

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();
    m_stopping = false;
    m_nonRecoverableFailure = false;
    updateStatusConfiguration();
    m_process.setProcessEnvironment(environment);
    m_process.setWorkingDirectory(root.absolutePath());
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_process.setProgram(m_options.pythonExecutable);
    m_process.setArguments(arguments);
    publishStatus(VisionRuntimeState::Starting, false, false, false,
                  QStringLiteral("视觉模型加载中"), 0.0, 0.0,
                  m_options.cameraIndex);
    m_startupTimer.start();
    m_process.start();
    return true;
}

void VisionAdapter::stop()
{
    m_retryTimer.stop();
    if (m_process.state() != QProcess::NotRunning)
        requestStop(false);
    else
        publishStatus(VisionRuntimeState::Disabled, false, false, false,
                      QStringLiteral("视觉感知已停止"), 0.0, 0.0,
                      m_options.cameraIndex);
}

bool VisionAdapter::restart()
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

bool VisionAdapter::setEnabled(bool enabled)
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

bool VisionAdapter::reconfigure(const Options& options, QString* error)
{
    if (options.cameraIndex < 0 || options.cameraIndex > 32
        || options.frameWidth < 64 || options.frameWidth > 1920
        || options.frameHeight < 48 || options.frameHeight > 1080
        || options.targetFps < 1 || options.targetFps > 30) {
        if (error)
            *error = QStringLiteral("视觉输入参数无效");
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

bool VisionAdapter::isRunning() const
{
    return m_process.state() != QProcess::NotRunning;
}

VisionStatus VisionAdapter::status() const
{
    return m_status;
}

VisionAdapter::Options VisionAdapter::options() const
{
    return m_options;
}

VisionAdapter::Options VisionAdapter::defaultOptions()
{
    Options options;
#ifdef Q_OS_LINUX
    options.enabled = true;
#endif
    options.enabled = environmentFlag(QByteArrayLiteral("LONGPET_VISION_ENABLED"),
                                      options.enabled);
    options.pythonExecutable = qEnvironmentVariable(
        "LONGPET_VISION_PYTHON", QStringLiteral("python3"));
    options.workerScript = qEnvironmentVariable(
        "LONGPET_VISION_WORKER", options.workerScript);
    options.cameraIndex = environmentInt(
        QByteArrayLiteral("LONGPET_VISION_CAMERA"), options.cameraIndex, 0, 32);
    options.frameWidth = environmentInt(
        QByteArrayLiteral("LONGPET_VISION_WIDTH"), options.frameWidth, 64, 1920);
    options.frameHeight = environmentInt(
        QByteArrayLiteral("LONGPET_VISION_HEIGHT"), options.frameHeight, 48, 1080);
    options.targetFps = environmentInt(
        QByteArrayLiteral("LONGPET_VISION_FPS"), options.targetFps, 1, 30);
    options.fallEnabled = environmentFlag(
        QByteArrayLiteral("LONGPET_VISION_FALL_ENABLED"), options.fallEnabled);
    options.waveEnabled = environmentFlag(
        QByteArrayLiteral("LONGPET_VISION_WAVE_ENABLED"), options.waveEnabled);
    options.startupTimeoutMs = environmentInt(
        QByteArrayLiteral("LONGPET_VISION_STARTUP_TIMEOUT_MS"),
        options.startupTimeoutMs, 1'000, 180'000);
    options.niceAdjustment = environmentInt(
        QByteArrayLiteral("LONGPET_VISION_NICE"), options.niceAdjustment, 0, 19);
    options.runtimeRoot = qEnvironmentVariable("LONGPET_VISION_ROOT");
    if (!options.runtimeRoot.isEmpty())
        return options;

    const QDir applicationDirectory(QCoreApplication::applicationDirPath());
    const QString adjacent = applicationDirectory.filePath(QStringLiteral("vision"));
    const QString installed = QDir(
        applicationDirectory.filePath(QStringLiteral("../share/longpet")))
                                  .filePath(QStringLiteral("vision"));
    const QString sourceTree = applicationDirectory.filePath(
        QStringLiteral("../third_party/longpet-vision"));
    if (QFileInfo::exists(adjacent))
        options.runtimeRoot = adjacent;
    else if (QFileInfo::exists(installed))
        options.runtimeRoot = installed;
    else
        options.runtimeRoot = sourceTree;
    return options;
}

bool VisionAdapter::parseDetectionEvent(const QByteArray& line,
                                        VisionDetection* detection)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(line.trimmed(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return false;
    const QJsonObject object = document.object();
    const QString event = object.value(QStringLiteral("event"))
                              .toString()
                              .trimmed()
                              .toLower();
    if (event != QStringLiteral("vision_detected")
        && event != QStringLiteral("vision_event")) {
        return false;
    }

    VisionDetection parsed;
    parsed.type = eventTypeFor(object.value(QStringLiteral("type")).toString());
    const QJsonValue confidence = object.value(QStringLiteral("confidence"));
    if (parsed.type == VisionEventType::Unknown || !confidence.isDouble())
        return false;
    parsed.confidence = confidence.toDouble();
    if (!std::isfinite(parsed.confidence) || parsed.confidence < 0.0
        || parsed.confidence > 1.0) {
        return false;
    }
    parsed.timestamp = parseTimestamp(object.value(QStringLiteral("timestamp")));
    parsed.source = object.value(QStringLiteral("source")).toString().trimmed();
    if (parsed.source.isEmpty())
        parsed.source = QStringLiteral("vision_worker");
    const QJsonValue trackId = object.value(QStringLiteral("track_id"));
    if (trackId.isString())
        parsed.trackId = trackId.toString().trimmed();
    else if (trackId.isDouble())
        parsed.trackId = QString::number(static_cast<qint64>(trackId.toDouble()));
    if (object.value(QStringLiteral("metadata")).isObject())
        parsed.metadata = object.value(QStringLiteral("metadata")).toObject();
    if (detection)
        *detection = parsed;
    return true;
}

bool VisionAdapter::parseRuntimeStatusEvent(const QByteArray& line,
                                            VisionStatus* status)
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

    VisionStatus parsed;
    const QString state = object.value(QStringLiteral("state"))
                              .toString()
                              .trimmed()
                              .toLower();
    if (state == QStringLiteral("running") || state == QStringLiteral("ready")
        || state == QStringLiteral("monitoring")) {
        parsed.state = VisionRuntimeState::Running;
        parsed.available = jsonBool(object, QStringLiteral("available"), true);
        parsed.cameraAvailable = jsonBool(
            object, QStringLiteral("camera_available"), true);
        parsed.monitoring = jsonBool(object, QStringLiteral("monitoring"), true);
        parsed.summary = statusSummary(object, QStringLiteral("本地视觉感知运行中"));
    } else if (state == QStringLiteral("starting")) {
        parsed.state = VisionRuntimeState::Starting;
        parsed.cameraAvailable = jsonBool(
            object, QStringLiteral("camera_available"), false);
        parsed.summary = statusSummary(object, QStringLiteral("视觉模型加载中"));
    } else if (state == QStringLiteral("degraded")
               || state == QStringLiteral("unavailable")
               || state == QStringLiteral("no_camera")) {
        parsed.state = VisionRuntimeState::Degraded;
        parsed.available = jsonBool(object, QStringLiteral("available"), false);
        parsed.cameraAvailable = jsonBool(
            object, QStringLiteral("camera_available"), false);
        parsed.monitoring = jsonBool(object, QStringLiteral("monitoring"), false);
        parsed.summary = statusSummary(object, QStringLiteral("视觉感知暂不可用"));
    } else if (state == QStringLiteral("error")) {
        parsed.state = VisionRuntimeState::Error;
        parsed.summary = statusSummary(object, QStringLiteral("视觉进程运行异常"));
    } else if (state == QStringLiteral("stopped")
               || state == QStringLiteral("stopping")
               || state == QStringLiteral("disabled")) {
        parsed.state = VisionRuntimeState::Disabled;
        parsed.summary = statusSummary(object, QStringLiteral("视觉感知已停止"));
    } else {
        return false;
    }

    parsed.effectiveFps = nonNegativeMetric(object, QStringLiteral("fps"));
    parsed.frameTimeMs = nonNegativeMetric(object, QStringLiteral("frame_ms"));
    parsed.cameraIndex = object.value(QStringLiteral("camera_index")).toInt(-1);
    if (status)
        *status = parsed;
    return true;
}

void VisionAdapter::readStandardOutput()
{
    m_stdoutBuffer += m_process.readAllStandardOutput();
    if (m_stdoutBuffer.size() > MaximumBufferedOutput
        && !m_stdoutBuffer.contains('\n')) {
        m_stdoutBuffer.clear();
        emit diagnosticMessage(QStringLiteral("视觉进程输出行过长，已丢弃"));
        return;
    }

    qsizetype newline = -1;
    while ((newline = m_stdoutBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_stdoutBuffer.left(newline).trimmed();
        m_stdoutBuffer.remove(0, newline + 1);
        if (line.isEmpty())
            continue;
        VisionDetection detection;
        if (parseDetectionEvent(line, &detection)) {
            emit detectionReceived(detection);
            continue;
        }
        VisionStatus runtimeStatus;
        if (parseRuntimeStatusEvent(line, &runtimeStatus)) {
            QJsonParseError statusError;
            const QJsonDocument statusDocument = QJsonDocument::fromJson(
                line, &statusError);
            if (statusError.error == QJsonParseError::NoError
                && statusDocument.isObject()) {
                m_nonRecoverableFailure = !statusDocument.object().value(
                    QStringLiteral("recoverable")).toBool(true);
            }
            if (runtimeStatus.state == VisionRuntimeState::Running
                || runtimeStatus.state == VisionRuntimeState::Degraded
                || runtimeStatus.state == VisionRuntimeState::Error) {
                m_startupTimer.stop();
                if (runtimeStatus.state == VisionRuntimeState::Running)
                    m_stabilityTimer.start();
            }
            publishStatus(runtimeStatus.state, runtimeStatus.available,
                          runtimeStatus.cameraAvailable, runtimeStatus.monitoring,
                          runtimeStatus.summary, runtimeStatus.effectiveFps,
                          runtimeStatus.frameTimeMs, runtimeStatus.cameraIndex);
            continue;
        }
        emit diagnosticMessage(QStringLiteral("忽略无法解析的视觉输出：%1")
                                   .arg(QString::fromUtf8(line)));
    }
}

void VisionAdapter::readStandardError()
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

void VisionAdapter::handleFinished(int exitCode, QProcess::ExitStatus exitStatus)
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
        publishStatus(VisionRuntimeState::Disabled, false, false, false,
                      QStringLiteral("视觉感知已停止"), 0.0, 0.0,
                      m_options.cameraIndex);
        if (shouldRestart)
            QTimer::singleShot(0, this, [this] { start(); });
        return;
    }
    const QString reason = exitStatus == QProcess::CrashExit
        ? QStringLiteral("视觉进程异常退出，LongPet 已降级运行")
        : QStringLiteral("视觉进程已退出（%1），LongPet 已降级运行").arg(exitCode);
    scheduleRecovery(reason);
}

void VisionAdapter::requestStop(bool restartAfterStop)
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

void VisionAdapter::forceKill()
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

void VisionAdapter::scheduleRecovery(const QString& reason)
{
    if (!m_options.enabled || m_nonRecoverableFailure
        || m_retryAttempt >= m_options.retryDelaysMs.size()) {
        publishStatus(m_nonRecoverableFailure
                          ? VisionRuntimeState::Error
                          : VisionRuntimeState::Degraded,
                      false, false, false,
                      m_nonRecoverableFailure ? reason
                          : QStringLiteral("%1；自动恢复次数已用尽").arg(reason),
                      0.0, 0.0, m_options.cameraIndex);
        return;
    }
    const int delay = qMax(1, m_options.retryDelaysMs.at(m_retryAttempt));
    ++m_retryAttempt;
    publishStatus(VisionRuntimeState::Degraded, false, false, false,
                  QStringLiteral("%1；%2 秒后第 %3 次重试")
                      .arg(reason).arg(delay / 1000.0, 0, 'f', 1)
                      .arg(m_retryAttempt),
                  0.0, 0.0, m_options.cameraIndex);
    emit recoveryScheduled(m_retryAttempt, delay);
    m_retryTimer.start(delay);
}

void VisionAdapter::updateStatusConfiguration()
{
    m_status.enabled = m_options.enabled;
    m_status.frameWidth = m_options.frameWidth;
    m_status.frameHeight = m_options.frameHeight;
    m_status.targetFps = m_options.targetFps;
    m_status.waveEnabled = m_options.waveEnabled;
    m_status.fallCandidateEnabled = m_options.fallEnabled;
    m_status.cameraIndex = m_options.cameraIndex;
}

void VisionAdapter::publishStatus(VisionRuntimeState state, bool available,
                                  bool cameraAvailable, bool monitoring,
                                  const QString& summary, double effectiveFps,
                                  double frameTimeMs, int cameraIndex)
{
    updateStatusConfiguration();
    VisionStatus next = m_status;
    next.state = state;
    next.enabled = m_options.enabled;
    next.available = available;
    next.running = m_process.state() != QProcess::NotRunning;
    next.cameraAvailable = cameraAvailable;
    next.monitoring = monitoring;
    next.summary = summary.simplified();
    next.effectiveFps = effectiveFps;
    next.frameTimeMs = frameTimeMs;
    next.cameraIndex = cameraIndex >= 0 ? cameraIndex : m_options.cameraIndex;
    next.errorDetail = (state == VisionRuntimeState::Degraded
                        || state == VisionRuntimeState::Error)
        ? next.summary : QString();
    if (sameStatus(next, m_status))
        return;
    m_status = next;
    emit statusChanged(m_status);
}

bool VisionAdapter::validateRuntime(QString* error) const
{
    if (m_options.cameraIndex < 0 || m_options.cameraIndex > 32
        || m_options.frameWidth < 64 || m_options.frameWidth > 1920
        || m_options.frameHeight < 48 || m_options.frameHeight > 1080
        || m_options.targetFps < 1 || m_options.targetFps > 30) {
        if (error)
            *error = QStringLiteral("视觉配置无效，拒绝启动 worker");
        return false;
    }
    const QFileInfo executable(m_options.pythonExecutable);
    if ((executable.isAbsolute() && !executable.isExecutable())
        || (!executable.isAbsolute()
            && QStandardPaths::findExecutable(m_options.pythonExecutable).isEmpty())) {
        if (error)
            *error = QStringLiteral("Vision Python 可执行文件不可用：%1")
                         .arg(m_options.pythonExecutable);
        return false;
    }
    const QDir root(m_options.runtimeRoot);
    if (!root.exists()) {
        if (error)
            *error = QStringLiteral("视觉运行时目录不存在：%1")
                         .arg(QDir::toNativeSeparators(root.absolutePath()));
        return false;
    }
    const QFileInfo worker(root.filePath(m_options.workerScript));
    if (!worker.exists() || !worker.isFile()) {
        if (error) {
            *error = QStringLiteral("视觉运行时不完整：缺少 %1")
                         .arg(m_options.workerScript);
        }
        return false;
    }
    return true;
}
