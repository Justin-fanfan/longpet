#pragma once

#include "model/DiagnosticsModels.h"

#include <QObject>
#include <QTimer>

#include <functional>

class AudioDeviceAdapter;
class CameraDeviceAdapter;
class DiagnosticsService;
class KeywordSpottingService;
class ReminderService;
class SystemService;
class VisionService;

class DeveloperService final : public QObject {
    Q_OBJECT

public:
    using BoolProbe = std::function<bool()>;
    using IntProbe = std::function<int()>;
    using StringProbe = std::function<QString()>;

    DeveloperService(KeywordSpottingService* kws,
                     VisionService* vision,
                     ReminderService* reminders,
                     SystemService* system,
                     AudioDeviceAdapter* audioDevices,
                     CameraDeviceAdapter* cameraDevices,
                     DiagnosticsService* diagnostics,
                     BoolProbe databaseAvailable = {},
                     IntProbe schemaVersion = {},
                     StringProbe databaseDetail = {},
                     QObject* parent = nullptr);

    DeveloperSnapshot snapshot() const;
    QList<DiagnosticEvent> events() const;

    bool setKwsEnabled(bool enabled);
    bool startKws();
    void stopKws();
    bool restartKws();
    bool reconfigureKws(const KeywordSpottingConfig& config,
                        QString* error = nullptr);
    bool setVisionEnabled(bool enabled);
    bool startVision();
    void stopVision();
    bool restartVision();
    bool reconfigureVision(const VisionConfig& config,
                           QString* error = nullptr);

    void simulateGreeting();
    void simulateAcknowledge();
    void simulateWave();
    void simulateReminderDue();
    void simulateEmergency();
    void recordControllerEvent(const QString& event, const QString& detail = {},
                               DiagnosticLevel level = DiagnosticLevel::Info);

signals:
    void snapshotChanged(const DeveloperSnapshot& snapshot);
    void diagnosticEventAdded(const DiagnosticEvent& event);
    void audioLevelChanged(double rms, double peak);

private:
    void connectDiagnostics();
    void record(DiagnosticSource source, DiagnosticLevel level,
                const QString& event, const QString& detail = {});

    KeywordSpottingService* m_kws = nullptr;
    VisionService* m_vision = nullptr;
    ReminderService* m_reminders = nullptr;
    SystemService* m_system = nullptr;
    AudioDeviceAdapter* m_audioDevices = nullptr;
    CameraDeviceAdapter* m_cameraDevices = nullptr;
    DiagnosticsService* m_diagnostics = nullptr;
    BoolProbe m_databaseAvailable;
    IntProbe m_schemaVersion;
    StringProbe m_databaseDetail;
    QTimer m_snapshotTimer;
    int m_lastKwsState = -1;
    int m_lastVisionState = -1;
    bool m_lastNetworkKnown = false;
    bool m_lastNetworkAvailable = false;
    QString m_lastNetworkSummary;
};
