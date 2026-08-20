#include "KeywordSpottingAdapter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QDebug>

#include <utility>

#ifdef Q_OS_LINUX
#include <signal.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace {
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
    m_startupTimer.setInterval(qMax(1, m_options.startupTimeoutMs));
    connect(&m_startupTimer, &QTimer::timeout, this, [this] {
        if (m_status.state != KeywordSpottingRuntimeState::Starting)
            return;
        publishStatus(KeywordSpottingRuntimeState::Error, false, false,
                      QStringLiteral("关键词模型启动超时"));
        m_stopping = true;
        m_process.terminate();
    });
    connect(&m_process, &QProcess::readyReadStandardOutput,
            this, &KeywordSpottingAdapter::readStandardOutput);
    connect(&m_process, &QProcess::readyReadStandardError,
            this, &KeywordSpottingAdapter::readStandardError);
    connect(&m_process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
        if (m_stopping || error == QProcess::Crashed)
            return;
        publishStatus(KeywordSpottingRuntimeState::Error, false, false,
                      QStringLiteral("无法启动关键词识别进程：%1")
                          .arg(m_process.errorString()));
    });
    connect(&m_process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &KeywordSpottingAdapter::handleFinished);
#ifdef Q_OS_LINUX
    m_process.setChildProcessModifier([] {
        const pid_t expectedParent = getppid();
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
    stop();
}

bool KeywordSpottingAdapter::start()
{
    if (m_process.state() != QProcess::NotRunning)
        return true;
    if (!m_options.enabled) {
        publishStatus(KeywordSpottingRuntimeState::Disabled, false, false,
                      QStringLiteral("关键词识别未启用"));
        return false;
    }

    QString error;
    if (!validateRuntime(&error)) {
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
        QStringLiteral("--score"), QString::number(m_options.score, 'f', 3)
    };
    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();
    m_stopping = false;
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
    m_startupTimer.stop();
    if (m_process.state() != QProcess::NotRunning) {
        m_stopping = true;
        m_process.terminate();
        if (!m_process.waitForFinished(3'500)) {
            m_process.kill();
            m_process.waitForFinished(1'000);
        }
    }
    m_stopping = false;
    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();
    if (m_status.state != KeywordSpottingRuntimeState::Error) {
        publishStatus(KeywordSpottingRuntimeState::Disabled, false, false,
                      QStringLiteral("关键词识别已停止"));
    }
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
        KeywordSpottingStatus runtimeStatus;
        if (parseRuntimeStatusEvent(line, &runtimeStatus)) {
            if (runtimeStatus.state == KeywordSpottingRuntimeState::Listening)
                m_startupTimer.stop();
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
    if (m_stopping)
        return;
    const QString reason = exitStatus == QProcess::CrashExit
        ? QStringLiteral("关键词识别进程异常退出")
        : QStringLiteral("关键词识别进程已退出（%1）").arg(exitCode);
    publishStatus(KeywordSpottingRuntimeState::Error, false, false, reason);
}

void KeywordSpottingAdapter::publishStatus(
    KeywordSpottingRuntimeState state, bool available, bool listening,
    const QString& summary)
{
    KeywordSpottingStatus next = m_status;
    next.state = state;
    next.available = available;
    next.listening = listening;
    next.summary = summary.simplified();
    if (next.state == m_status.state && next.available == m_status.available
        && next.listening == m_status.listening
        && next.summary == m_status.summary) {
        return;
    }
    m_status = next;
    emit statusChanged(m_status);
}

bool KeywordSpottingAdapter::validateRuntime(QString* error) const
{
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
