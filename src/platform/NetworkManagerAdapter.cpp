#include "NetworkManagerAdapter.h"

#include <QHash>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>

#include <algorithm>
#include <memory>

namespace {
QString defaultExecutable()
{
    const QString overridePath = qEnvironmentVariable("LONGPET_NMCLI_PATH");
    if (!overridePath.isEmpty())
        return overridePath;
#ifdef Q_OS_WIN
    return QStringLiteral("nmcli");
#else
    return QStringLiteral("/bin/nmcli");
#endif
}

QString defaultInterfaceName()
{
    const QString overrideName = qEnvironmentVariable("LONGPET_WIFI_INTERFACE");
    return overrideName.isEmpty() ? QStringLiteral("wlan0") : overrideName;
}

QStringList splitTerseLine(const QString& line)
{
    QStringList fields;
    QString current;
    bool escaped = false;
    for (const QChar character : line) {
        if (escaped) {
            current.append(character);
            escaped = false;
        } else if (character == QLatin1Char('\\')) {
            escaped = true;
        } else if (character == QLatin1Char(':')) {
            fields.append(current);
            current.clear();
        } else {
            current.append(character);
        }
    }
    if (escaped)
        current.append(QLatin1Char('\\'));
    fields.append(current);
    return fields;
}

bool isEnterpriseSecurity(const QString& security)
{
    return security.contains(QStringLiteral("802.1X"), Qt::CaseInsensitive)
        || security.contains(QStringLiteral("EAP"), Qt::CaseInsensitive);
}
}

NetworkManagerAdapter::NetworkManagerAdapter(QObject* parent)
    : NetworkManagerAdapter(defaultExecutable(), defaultInterfaceName(), parent)
{
}

NetworkManagerAdapter::NetworkManagerAdapter(const QString& executable,
                                             const QString& interfaceName,
                                             QObject* parent)
    : QObject(parent),
      m_executable(executable),
      m_interfaceName(interfaceName)
{
}

NetworkManagerAdapter::~NetworkManagerAdapter()
{
    stopProcesses();
}

bool NetworkManagerAdapter::isBusy() const
{
    return !m_scanProcess.isNull() || !m_connectProcess.isNull();
}

QString NetworkManagerAdapter::interfaceName() const
{
    return m_interfaceName;
}

QList<WifiNetwork> NetworkManagerAdapter::parseScanOutput(const QByteArray& output)
{
    QList<WifiNetwork> networks;
    QHash<QString, int> indexBySsid;
    const QString text = QString::fromUtf8(output);
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (QString line : lines) {
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        const QStringList fields = splitTerseLine(line);
        if (fields.size() != 4)
            continue;

        const QString ssid = fields.at(1);
        if (ssid.isEmpty())
            continue;

        bool validSignal = false;
        const int signal = fields.at(2).toInt(&validSignal);
        const QString security = fields.at(3).trimmed();
        WifiNetwork network;
        network.ssid = ssid;
        network.signalStrength = validSignal ? qBound(0, signal, 100) : 0;
        network.security = security == QStringLiteral("--") ? QString() : security;
        network.connected = fields.at(0).trimmed() == QStringLiteral("*");
        network.requiresPassword = !network.security.isEmpty();
        network.supported = !isEnterpriseSecurity(network.security);

        const auto existing = indexBySsid.constFind(ssid);
        if (existing == indexBySsid.cend()) {
            indexBySsid.insert(ssid, networks.size());
            networks.append(network);
        } else {
            WifiNetwork& previous = networks[*existing];
            if ((!previous.connected && network.connected)
                || (previous.connected == network.connected
                    && network.signalStrength > previous.signalStrength)) {
                previous = network;
            }
        }
    }

    std::sort(networks.begin(), networks.end(),
              [](const WifiNetwork& left, const WifiNetwork& right) {
        if (left.connected != right.connected)
            return left.connected;
        if (left.signalStrength != right.signalStrength)
            return left.signalStrength > right.signalStrength;
        return QString::localeAwareCompare(left.ssid, right.ssid) < 0;
    });
    return networks;
}

void NetworkManagerAdapter::scanNetworks()
{
    if (isBusy()) {
        emit scanFailed(QStringLiteral("网络操作正在进行，请稍候"));
        return;
    }

    QProcess* process = createProcess();
    m_scanProcess = process;
    const auto reported = std::make_shared<bool>(false);
    connect(process, &QProcess::errorOccurred, this,
            [this, process, reported](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || *reported)
            return;
        *reported = true;
        if (m_scanProcess == process)
            m_scanProcess.clear();
        emit scanFailed(QStringLiteral("无法启动 NetworkManager 客户端：%1")
                            .arg(process->errorString()));
        process->deleteLater();
    });
    connect(process, &QProcess::finished, this,
            [this, process, reported](int exitCode, QProcess::ExitStatus exitStatus) {
        if (*reported)
            return;
        *reported = true;
        const QByteArray output = process->readAll();
        if (m_scanProcess == process)
            m_scanProcess.clear();
        if (exitStatus != QProcess::NormalExit || exitCode != 0) {
            emit scanFailed(friendlyError(output, QStringLiteral("扫描 Wi-Fi 失败")));
        } else {
            emit scanCompleted(parseScanOutput(output));
        }
        process->deleteLater();
    });

    emit scanStarted();
    process->start(m_executable, {
        QStringLiteral("--terse"),
        QStringLiteral("--escape"), QStringLiteral("yes"),
        QStringLiteral("--colors"), QStringLiteral("no"),
        QStringLiteral("--wait"), QStringLiteral("12"),
        QStringLiteral("--fields"),
        QStringLiteral("IN-USE,SSID,SIGNAL,SECURITY"),
        QStringLiteral("device"), QStringLiteral("wifi"),
        QStringLiteral("list"),
        QStringLiteral("ifname"), m_interfaceName,
        QStringLiteral("--rescan"), QStringLiteral("yes")
    });
}

void NetworkManagerAdapter::connectToNetwork(const WifiNetwork& network,
                                             const QString& password)
{
    if (isBusy()) {
        emit connectionFailed(network.ssid,
                              QStringLiteral("网络操作正在进行，请稍候"));
        return;
    }

    QProcess* process = createProcess();
    m_connectProcess = process;
    const auto reported = std::make_shared<bool>(false);
    const auto secret = std::make_shared<QString>(password);
    connect(process, &QProcess::started, process,
            [process, secret, requiresPassword = network.requiresPassword] {
        if (requiresPassword) {
            QByteArray bytes = secret->toUtf8();
            process->write(bytes);
            process->write("\n");
            bytes.fill('\0');
        }
        process->closeWriteChannel();
        secret->fill(QChar());
        secret->clear();
    });
    connect(process, &QProcess::errorOccurred, this,
            [this, process, reported, secret,
             ssid = network.ssid](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || *reported)
            return;
        secret->fill(QChar());
        secret->clear();
        *reported = true;
        if (m_connectProcess == process)
            m_connectProcess.clear();
        emit connectionFailed(ssid,
            QStringLiteral("无法启动 NetworkManager 客户端：%1")
                .arg(process->errorString()));
        process->deleteLater();
    });
    connect(process, &QProcess::finished, this,
            [this, process, reported, ssid = network.ssid](
                int exitCode, QProcess::ExitStatus exitStatus) {
        if (*reported)
            return;
        *reported = true;
        const QByteArray output = process->readAll();
        if (m_connectProcess == process)
            m_connectProcess.clear();
        if (exitStatus != QProcess::NormalExit || exitCode != 0) {
            emit connectionFailed(ssid,
                friendlyError(output, QStringLiteral("连接 Wi-Fi 失败")));
        } else {
            emit connectionSucceeded(ssid);
        }
        process->deleteLater();
    });

    emit connectionStarted(network.ssid);
    process->start(m_executable, {
        QStringLiteral("--colors"), QStringLiteral("no"),
        QStringLiteral("--wait"), QStringLiteral("35"),
        QStringLiteral("--ask"),
        QStringLiteral("device"), QStringLiteral("wifi"),
        QStringLiteral("connect"), network.ssid,
        QStringLiteral("ifname"), m_interfaceName
    });
}

QProcess* NetworkManagerAdapter::createProcess()
{
    auto* process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    environment.insert(QStringLiteral("LANG"), QStringLiteral("C"));
    environment.insert(QStringLiteral("NO_COLOR"), QStringLiteral("1"));
    process->setProcessEnvironment(environment);
    return process;
}

void NetworkManagerAdapter::stopProcesses()
{
    for (QPointer<QProcess> process : {m_scanProcess, m_connectProcess}) {
        if (!process || process->state() == QProcess::NotRunning)
            continue;
        process->kill();
        process->waitForFinished(1'000);
    }
    m_scanProcess.clear();
    m_connectProcess.clear();
}

QString NetworkManagerAdapter::friendlyError(const QByteArray& output,
                                             const QString& fallback) const
{
    QString detail = QString::fromUtf8(output).simplified();
    if (detail.contains(QStringLiteral("Secrets were required"), Qt::CaseInsensitive)
        || detail.contains(QStringLiteral("wrong password"), Qt::CaseInsensitive)) {
        return QStringLiteral("密码错误或认证信息无效");
    }
    if (detail.contains(QStringLiteral("No network with SSID"), Qt::CaseInsensitive))
        return QStringLiteral("该网络已不在扫描范围内，请刷新后重试");
    if (detail.contains(QStringLiteral("not authorized"), Qt::CaseInsensitive)
        || detail.contains(QStringLiteral("permission"), Qt::CaseInsensitive)) {
        return QStringLiteral("当前用户没有配置网络的权限");
    }
    if (detail.startsWith(QStringLiteral("Error:"), Qt::CaseInsensitive))
        detail = detail.mid(6).trimmed();
    if (detail.size() > 240)
        detail = detail.left(237) + QStringLiteral("...");
    return detail.isEmpty() ? fallback : detail;
}
