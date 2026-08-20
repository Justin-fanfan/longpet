#pragma once

#include "model/DiagnosticsModels.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QProgressBar;
class QSpinBox;
class QDoubleSpinBox;
class QTextEdit;

class DeveloperPage final : public QWidget {
    Q_OBJECT

public:
    explicit DeveloperPage(QWidget* parent = nullptr);
    void setSnapshot(const DeveloperSnapshot& snapshot);
    void setEvents(const QList<DiagnosticEvent>& events);
    void appendEvent(const DiagnosticEvent& event);
    void setAudioLevel(double rms, double peak);

signals:
    void backRequested();
    void kwsEnableRequested(bool enabled);
    void kwsStartRequested();
    void kwsStopRequested();
    void kwsRestartRequested();
    void kwsReconfigureRequested(const KeywordSpottingConfig& config);
    void visionEnableRequested(bool enabled);
    void visionStartRequested();
    void visionStopRequested();
    void visionRestartRequested();
    void visionReconfigureRequested(const VisionConfig& config);
    void simulationRequested(DeveloperSimulation simulation);

private:
    QWidget* createOverviewTab();
    QWidget* createKwsTab();
    QWidget* createVisionTab();
    QWidget* createDevicesTab();
    QWidget* createEventsTab();
    static QString eventText(const DiagnosticEvent& event);
    void updateDeviceChoices(const DeveloperSnapshot& snapshot);

    QLabel* m_overview = nullptr;
    QLabel* m_kwsState = nullptr;
    QLabel* m_kwsMetrics = nullptr;
    QLabel* m_kwsLast = nullptr;
    QProgressBar* m_rms = nullptr;
    QProgressBar* m_peak = nullptr;
    QComboBox* m_audioDevice = nullptr;
    QSpinBox* m_sampleRate = nullptr;
    QSpinBox* m_channels = nullptr;
    QSpinBox* m_micChannel = nullptr;
    QDoubleSpinBox* m_threshold = nullptr;
    QDoubleSpinBox* m_score = nullptr;
    QCheckBox* m_kwsEnabled = nullptr;
    QLabel* m_visionState = nullptr;
    QLabel* m_visionMetrics = nullptr;
    QLabel* m_visionLast = nullptr;
    QComboBox* m_camera = nullptr;
    QSpinBox* m_width = nullptr;
    QSpinBox* m_height = nullptr;
    QSpinBox* m_fps = nullptr;
    QCheckBox* m_wave = nullptr;
    QCheckBox* m_fall = nullptr;
    QCheckBox* m_visionEnabled = nullptr;
    QTextEdit* m_devices = nullptr;
    QListWidget* m_events = nullptr;
    QListWidget* m_kwsEvents = nullptr;
    DeveloperSnapshot m_snapshot;
    bool m_kwsControlsInitialized = false;
    bool m_visionControlsInitialized = false;
};
