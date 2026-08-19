#include "BacklightAdapter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <algorithm>

BacklightAdapter::BacklightAdapter(QObject* parent)
    : BacklightAdapter(QStringLiteral("/sys/class/backlight"), parent)
{
}

BacklightAdapter::BacklightAdapter(const QString& sysfsRoot, QObject* parent)
    : QObject(parent),
      m_sysfsRoot(sysfsRoot)
{
}

bool BacklightAdapter::start()
{
    if (m_available)
        return true;

    const QString overridePath = qEnvironmentVariable("LONGPET_BACKLIGHT_DEVICE");
    if (!overridePath.isEmpty()) {
        m_devicePath = overridePath;
    } else {
        const QDir root(m_sysfsRoot);
        const QStringList devices = root.entryList(
            QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        if (!devices.isEmpty())
            m_devicePath = root.filePath(devices.first());
    }

    QString error;
    if (m_devicePath.isEmpty()
        || !readInteger(QStringLiteral("max_brightness"), &m_maximum, &error)
        || m_maximum <= 0) {
        publishUnavailable(error.isEmpty()
            ? QStringLiteral("未检测到可用背光设备") : error);
        return false;
    }

    m_available = true;
    m_summary = m_maximum == 1
        ? QStringLiteral("GPIO 背光 · 仅开/关")
        : QStringLiteral("系统背光 · %1 级").arg(m_maximum + 1);
    emit controlStateChanged(true, m_maximum + 1, m_summary);
    return true;
}

void BacklightAdapter::stop()
{
    m_available = false;
    m_maximum = 0;
    m_devicePath.clear();
    m_summary.clear();
}

bool BacklightAdapter::isAvailable() const
{
    return m_available;
}

int BacklightAdapter::brightnessLevels() const
{
    return m_available ? m_maximum + 1 : 0;
}

QString BacklightAdapter::devicePath() const
{
    return m_devicePath;
}

int BacklightAdapter::percentToRaw(int percent, int maximum)
{
    if (maximum <= 0)
        return 0;
    const int bounded = std::clamp(percent, 0, 100);
    if (maximum == 1)
        return bounded == 0 ? 0 : 1;
    return (maximum * bounded + 50) / 100;
}

int BacklightAdapter::rawToPercent(int raw, int maximum)
{
    if (maximum <= 0)
        return 0;
    const int bounded = std::clamp(raw, 0, maximum);
    return (bounded * 100 + maximum / 2) / maximum;
}

void BacklightAdapter::applyBrightness(int percent)
{
    if (!m_available && !start())
        return;

    const int raw = percentToRaw(percent, m_maximum);
    QFile brightnessFile(QDir(m_devicePath).filePath(QStringLiteral("brightness")));
    if (!brightnessFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        const QString error = QStringLiteral("写入背光失败：%1")
            .arg(brightnessFile.errorString());
        emit controlStateChanged(false, 0, error);
        emit errorOccurred(error);
        return;
    }
    if (brightnessFile.write(QByteArray::number(raw)) < 0) {
        const QString error = QStringLiteral("写入背光失败：%1")
            .arg(brightnessFile.errorString());
        emit controlStateChanged(false, 0, error);
        emit errorOccurred(error);
        return;
    }
    brightnessFile.close();

    int applied = raw;
    readInteger(QStringLiteral("actual_brightness"), &applied);
    emit controlStateChanged(true, m_maximum + 1, m_summary);
    emit brightnessApplied(rawToPercent(applied, m_maximum));
}

bool BacklightAdapter::readInteger(const QString& fileName, int* value,
                                   QString* error) const
{
    const QString path = QDir(m_devicePath).filePath(fileName);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("读取 %1 失败：%2").arg(path, file.errorString());
        return false;
    }
    bool valid = false;
    const int result = QString::fromLatin1(file.readAll()).trimmed().toInt(&valid);
    if (!valid && error)
        *error = QStringLiteral("%1 不是有效整数").arg(path);
    if (valid && value)
        *value = result;
    return valid;
}

void BacklightAdapter::publishUnavailable(const QString& detail)
{
    m_available = false;
    m_summary.clear();
    emit controlStateChanged(false, 0, detail);
}
