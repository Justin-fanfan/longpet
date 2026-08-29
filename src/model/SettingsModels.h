#pragma once

#include <QString>

#include <optional>

struct UserSettings {
    int volume = 60;
    int brightness = 72;
    QString petStyle = QStringLiteral("温和陪伴");
};

struct SettingsUpdateRequest {
    std::optional<int> volume;
    std::optional<int> brightness;
    std::optional<QString> petStyle;
    int expectedRevision = 0;

    bool isEmpty() const
    {
        return !volume.has_value() && !brightness.has_value() && !petStyle.has_value();
    }
};

enum class SettingsUpdateErrorCode {
    None,
    Validation,
    CapabilityUnavailable,
    RevisionConflict,
    Storage
};

struct SettingsUpdateResult {
    bool success = false;
    bool revisionConflict = false;
    QString error;
    int revision = 0;
    UserSettings settings;
    SettingsUpdateErrorCode code = SettingsUpdateErrorCode::None;
};

struct DeviceSummary {
    QString softwareVersion;
    QString networkSummary;
    QString familySummary;
    bool audioControlAvailable = false;
    QString audioSummary;
    bool brightnessControlAvailable = false;
    int brightnessLevels = 0;
    QString brightnessSummary;
    QString powerSummary;
};
