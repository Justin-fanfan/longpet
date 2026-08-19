#include "PowerStatusAdapter.h"

#include <QDir>
#include <QFile>

PowerStatusAdapter::PowerStatusAdapter(QObject* parent)
    : PowerStatusAdapter(QStringLiteral("/sys/class/power_supply"), 30'000, parent)
{
}

PowerStatusAdapter::PowerStatusAdapter(const QString& sysfsRoot,
                                       int refreshIntervalMs,
                                       QObject* parent)
    : QObject(parent),
      m_sysfsRoot(sysfsRoot)
{
    m_refreshTimer.setInterval(qMax(1, refreshIntervalMs));
    m_refreshTimer.setTimerType(Qt::VeryCoarseTimer);
    connect(&m_refreshTimer, &QTimer::timeout,
            this, &PowerStatusAdapter::refresh);
}

bool PowerStatusAdapter::start()
{
    stop();
    const QDir root(m_sysfsRoot);
    const QStringList supplies = root.entryList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& supply : supplies) {
        const QString candidate = root.filePath(supply);
        if (readText(QDir(candidate).filePath(QStringLiteral("type")))
                .compare(QStringLiteral("Battery"), Qt::CaseInsensitive) == 0
            && QFile::exists(QDir(candidate).filePath(QStringLiteral("capacity")))) {
            m_batteryPath = candidate;
            break;
        }
    }

    refresh();
    if (!m_batteryPath.isEmpty())
        m_refreshTimer.start();
    return !m_batteryPath.isEmpty();
}

void PowerStatusAdapter::stop()
{
    m_refreshTimer.stop();
    m_batteryPath.clear();
}

bool PowerStatusAdapter::hasBattery() const
{
    return !m_batteryPath.isEmpty();
}

QString PowerStatusAdapter::batteryPath() const
{
    return m_batteryPath;
}

void PowerStatusAdapter::refresh()
{
    if (m_batteryPath.isEmpty()) {
        emit batteryPercentChanged(-1);
        emit powerStateChanged(QStringLiteral("未检测到电池"));
        return;
    }

    bool valid = false;
    const int capacity = readText(
        QDir(m_batteryPath).filePath(QStringLiteral("capacity"))).toInt(&valid);
    if (!valid) {
        emit batteryPercentChanged(-1);
        emit powerStateChanged(QStringLiteral("电池状态未知"));
        return;
    }

    const int bounded = qBound(0, capacity, 100);
    const QString status = readText(
        QDir(m_batteryPath).filePath(QStringLiteral("status")));
    emit batteryPercentChanged(bounded);
    emit powerStateChanged(status.isEmpty()
        ? QStringLiteral("电池 %1%").arg(bounded)
        : QStringLiteral("%1 · %2%").arg(status).arg(bounded));
}

QString PowerStatusAdapter::readText(const QString& path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromLatin1(file.readAll()).trimmed();
}
