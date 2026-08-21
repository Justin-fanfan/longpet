#pragma once

#include "model/KeywordSpottingModels.h"
#include "model/SystemModels.h"
#include "model/SettingsModels.h"
#include "model/VisionModels.h"

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QString>

enum class DiagnosticSource {
    Audio,
    KwsAdapter,
    KwsService,
    VisionAdapter,
    VisionService,
    Reminder,
    Controller,
    Database,
    System,
    Ui
};

enum class DiagnosticLevel { Debug, Info, Warning, Error };
enum class DeveloperSimulation { Greeting, Acknowledge, Wave, ReminderDue, Emergency };

struct DiagnosticEvent {
    QDateTime timestamp;
    DiagnosticSource source = DiagnosticSource::System;
    DiagnosticLevel level = DiagnosticLevel::Info;
    QString event;
    QString detail;
};

struct AudioInputDevice {
    QString id;
    QString displayName;
    bool available = false;
};

struct CameraDevice {
    QString id;
    int index = -1;
    QString displayName;
    bool available = false;
};

struct DeveloperSnapshot {
    KeywordSpottingStatus kws;
    VisionStatus vision;
    SystemStatus system;
    DeviceSummary device;
    QList<AudioInputDevice> audioInputs;
    QList<CameraDevice> cameras;
    bool sqliteAvailable = false;
    int sqliteSchemaVersion = 0;
    QString sqliteDetail;
    bool remoteAiAvailable = false;
    bool motionMcuAvailable = false;
    QDateTime capturedAt;
};

Q_DECLARE_METATYPE(DiagnosticEvent)
Q_DECLARE_METATYPE(DiagnosticSource)
Q_DECLARE_METATYPE(DiagnosticLevel)
Q_DECLARE_METATYPE(DeveloperSimulation)
Q_DECLARE_METATYPE(AudioInputDevice)
Q_DECLARE_METATYPE(CameraDevice)
Q_DECLARE_METATYPE(DeveloperSnapshot)
