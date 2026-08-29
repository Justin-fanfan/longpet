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

int SettingsService::revision(QString* error) const
{
    return m_repository->revision(error);
}

SettingsUpdateResult SettingsService::updateSettings(const SettingsUpdateRequest& request)
{
    SettingsUpdateResult result;
    if (request.isEmpty()) {
        result.error = QStringLiteral("没有可保存的设置字段");
        result.code = SettingsUpdateErrorCode::Validation;
        return result;
    }

    QHash<QString, QString> values;
    if (request.volume.has_value()) {
        if (*request.volume < 0 || *request.volume > 100) {
            result.error = QStringLiteral("音量必须在 0 到 100 之间");
            result.code = SettingsUpdateErrorCode::Validation;
            return result;
        }
        values.insert(QStringLiteral("volume"), QString::number(*request.volume));
    }
    if (request.brightness.has_value()) {
        if (*request.brightness < 0 || *request.brightness > 100) {
            result.error = QStringLiteral("亮度必须在 0 到 100 之间");
            result.code = SettingsUpdateErrorCode::Validation;
            return result;
        }
        values.insert(QStringLiteral("brightness"), QString::number(*request.brightness));
    }
    if (request.petStyle.has_value()) {
        static const QStringList supported {
            QStringLiteral("温和陪伴"),
            QStringLiteral("活泼陪伴")
        };
        if (!supported.contains(*request.petStyle)) {
            result.error = QStringLiteral("不支持的宠物陪伴风格");
            result.code = SettingsUpdateErrorCode::Validation;
            return result;
        }
        values.insert(QStringLiteral("pet_style"), *request.petStyle);
    }

    const SettingsRepositoryWriteResult writeResult = m_repository->setValues(
        values, request.expectedRevision);
    result.revisionConflict = writeResult.revisionConflict;
    result.revision = writeResult.revision;
    result.error = writeResult.error;
    result.code = writeResult.revisionConflict
        ? SettingsUpdateErrorCode::RevisionConflict
        : SettingsUpdateErrorCode::Storage;
    if (!writeResult.success)
        return result;

    emitCurrentSettings();
    if (request.volume.has_value())
        emit settingApplyRequested(QStringLiteral("volume"), *request.volume);
    if (request.brightness.has_value())
        emit settingApplyRequested(QStringLiteral("brightness"), *request.brightness);

    QString settingsError;
    result.settings = settings(&settingsError);
    if (!settingsError.isEmpty()) {
        result.error = settingsError;
        result.code = SettingsUpdateErrorCode::Storage;
        return result;
    }
    result.success = true;
    result.code = SettingsUpdateErrorCode::None;
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
