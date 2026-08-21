#include "SettingsService.h"

#include "data/SettingsRepository.h"

SettingsService::SettingsService(SettingsRepository* repository, QObject* parent)
    : QObject(parent), m_repository(repository)
{
}

UserSettings SettingsService::settings(QString* error) const
{
    UserSettings result;
    const QHash<QString, QString> values = m_repository->all(error);
    bool valid = false;
    const int volume = values.value(QStringLiteral("volume")).toInt(&valid);
    if (valid)
        result.volume = qBound(0, volume, 100);
    const int brightness = values.value(QStringLiteral("brightness")).toInt(&valid);
    if (valid)
        result.brightness = qBound(0, brightness, 100);
    if (!values.value(QStringLiteral("pet_style")).isEmpty())
        result.petStyle = values.value(QStringLiteral("pet_style"));
    return result;
}

bool SettingsService::setVolume(int value, QString* error)
{
    if (!setInteger(QStringLiteral("volume"), value, error))
        return false;
    emit settingApplyRequested(QStringLiteral("volume"), value);
    return true;
}

bool SettingsService::setBrightness(int value, QString* error)
{
    if (!setInteger(QStringLiteral("brightness"), value, error))
        return false;
    emit settingApplyRequested(QStringLiteral("brightness"), value);
    return true;
}

bool SettingsService::setPetStyle(const QString& style, QString* error)
{
    static const QStringList supported {
        QStringLiteral("温和陪伴"),
        QStringLiteral("活泼陪伴")
    };
    if (!supported.contains(style)) {
        if (error)
            *error = QStringLiteral("不支持的宠物陪伴风格");
        return false;
    }
    if (!m_repository->setValue(QStringLiteral("pet_style"), style, error))
        return false;
    emitCurrentSettings();
    return true;
}

// 温度单位
bool SettingsService::setTemperatureUnit(const QString& unit, QString* error)
{
    if (!SUPPORTED_TEMPERATURE_UNITS.contains(unit)) {
        if (error)
            *error = QStringLiteral("不支持的温度单位，请使用 'celsius' 或 'fahrenheit'");
        return false;
    }
    if (!m_repository->setValue(KEY_TEMPERATURE_UNIT, unit, error))
        return false;

    emitCurrentSettings();
    emit settingApplyRequested(KEY_TEMPERATURE_UNIT, unit);
    return true;
}
//

bool SettingsService::setInteger(const QString& key, int value, QString* error)
{
    if (value < 0 || value > 100) {
        if (error)
            *error = QStringLiteral("设置值必须在 0 到 100 之间");
        return false;
    }
    if (!m_repository->setValue(key, QString::number(value), error))
        return false;
    emitCurrentSettings();
    return true;
}

void SettingsService::emitCurrentSettings()
{
    emit settingsChanged(settings());
}

