#pragma once

#include "model/SettingsModels.h"

#include <QObject>
#include <QVariant>

class SettingsRepository;

class SettingsService final : public QObject {
    Q_OBJECT

public:
    explicit SettingsService(SettingsRepository* repository, QObject* parent = nullptr);

    UserSettings settings(QString* error = nullptr) const;
    bool setVolume(int value, QString* error = nullptr);
    bool setBrightness(int value, QString* error = nullptr);
    bool setPetStyle(const QString& style, QString* error = nullptr);

signals:
    void settingsChanged(const UserSettings& settings);
    void settingApplyRequested(const QString& key, const QVariant& value);

private:
    bool setInteger(const QString& key, int value, QString* error);
    void emitCurrentSettings();

    SettingsRepository* m_repository = nullptr;
};
