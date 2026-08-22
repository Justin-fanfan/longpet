#include "DeveloperPage.h"

#include "widgets/VisualComponents.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextEdit>
#include <QVBoxLayout>

namespace {
QPushButton* actionButton(const QString& text, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    button->setProperty("role", "secondaryCompact");
    button->setMinimumHeight(44);
    return button;
}

QString yesNo(bool value) { return value ? QStringLiteral("Yes") : QStringLiteral("No"); }

QString kwsStateName(KeywordSpottingRuntimeState state)
{
    switch (state) {
    case KeywordSpottingRuntimeState::Disabled: return QStringLiteral("Disabled");
    case KeywordSpottingRuntimeState::Starting: return QStringLiteral("Starting");
    case KeywordSpottingRuntimeState::Listening: return QStringLiteral("Listening");
    case KeywordSpottingRuntimeState::Degraded: return QStringLiteral("Degraded");
    case KeywordSpottingRuntimeState::Error: return QStringLiteral("Error");
    }
    return QStringLiteral("Unknown");
}

QString visionStateName(VisionRuntimeState state)
{
    switch (state) {
    case VisionRuntimeState::Disabled: return QStringLiteral("Disabled");
    case VisionRuntimeState::Starting: return QStringLiteral("Starting");
    case VisionRuntimeState::Running: return QStringLiteral("Running");
    case VisionRuntimeState::Degraded: return QStringLiteral("Degraded");
    case VisionRuntimeState::Error: return QStringLiteral("Error");
    }
    return QStringLiteral("Unknown");
}

QString visionEventName(VisionEventType type)
{
    switch (type) {
    case VisionEventType::Wave: return QStringLiteral("Wave");
    case VisionEventType::FallCandidate: return QStringLiteral("Fall candidate");
    case VisionEventType::FallConfirmed: return QStringLiteral("Fall confirmed");
    case VisionEventType::PersonDetected: return QStringLiteral("Person (reserved)");
    case VisionEventType::Unknown: return QStringLiteral("--");
    }
    return QStringLiteral("--");
}

QString sourceName(DiagnosticSource source)
{
    switch (source) {
    case DiagnosticSource::Audio: return QStringLiteral("AUDIO");
    case DiagnosticSource::KwsAdapter: return QStringLiteral("KWS-ADAPTER");
    case DiagnosticSource::KwsService: return QStringLiteral("KWS-SERVICE");
    case DiagnosticSource::VisionAdapter: return QStringLiteral("VISION-ADAPTER");
    case DiagnosticSource::VisionService: return QStringLiteral("VISION-SERVICE");
    case DiagnosticSource::Reminder: return QStringLiteral("REMINDER");
    case DiagnosticSource::Controller: return QStringLiteral("CONTROLLER");
    case DiagnosticSource::Database: return QStringLiteral("DATABASE");
    case DiagnosticSource::System: return QStringLiteral("SYSTEM");
    case DiagnosticSource::Ui: return QStringLiteral("UI");
    }
    return QStringLiteral("UNKNOWN");
}

QString levelName(DiagnosticLevel level)
{
    switch (level) {
    case DiagnosticLevel::Debug: return QStringLiteral("DEBUG");
    case DiagnosticLevel::Info: return QStringLiteral("INFO");
    case DiagnosticLevel::Warning: return QStringLiteral("WARN");
    case DiagnosticLevel::Error: return QStringLiteral("ERROR");
    }
    return QStringLiteral("INFO");
}

QString uptimeText(const QDateTime& startedAt)
{
    if (!startedAt.isValid())
        return QStringLiteral("--");
    const qint64 seconds = qMax<qint64>(0, startedAt.secsTo(QDateTime::currentDateTime()));
    return QStringLiteral("%1:%2:%3")
        .arg(seconds / 3600, 2, 10, QLatin1Char('0'))
        .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}
}

DeveloperPage::DeveloperPage(QWidget* parent)
    : QWidget(parent)
{
    setProperty("page", true);
    setAttribute(Qt::WA_StyledBackground, true);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto* header = new PageHeaderWidget(QStringLiteral("开发者诊断"), this);
    root->addWidget(header);
    connect(header, &PageHeaderWidget::backRequested,
            this, &DeveloperPage::backRequested);
    auto* tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("developerTabs"));
    tabs->setStyleSheet(QStringLiteral(
        "QTabWidget::pane { border: 0; background: #0E0F0F; }"
        "QTabBar::tab { min-width: 104px; min-height: 40px; padding: 0 18px; "
        "background: #25231F; color: #F7F2E8; border: 1px solid #3B3832; "
        "font-size: 18px; font-weight: 600; }"
        "QTabBar::tab:selected { background: #F7B844; color: #151515; }"
        "QTabWidget QWidget { background: #0E0F0F; color: #F7F2E8; }"
        "QTabWidget QLabel, QTabWidget QCheckBox { color: #F7F2E8; font-size: 16px; }"
        "QTabWidget QComboBox, QTabWidget QSpinBox, QTabWidget QDoubleSpinBox, "
        "QTabWidget QTextEdit, QTabWidget QListWidget { background: #25231F; "
        "color: #F7F2E8; border: 1px solid #4A463F; padding: 4px; font-size: 15px; }"
        "QTabWidget QProgressBar { background: #25231F; color: #F7F2E8; "
        "border: 1px solid #4A463F; text-align: center; }"
        "QTabWidget QProgressBar::chunk { background: #F7B844; }"));
    tabs->addTab(createOverviewTab(), QStringLiteral("总览"));
    tabs->addTab(createKwsTab(), QStringLiteral("语音"));
    tabs->addTab(createVisionTab(), QStringLiteral("视觉"));
    tabs->addTab(createDevicesTab(), QStringLiteral("设备"));
    tabs->addTab(createEventsTab(), QStringLiteral("事件"));
    root->addWidget(tabs, 1);
}

QWidget* DeveloperPage::createOverviewTab()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 12, 24, 16);
    m_overview = new QLabel(page);
    m_overview->setObjectName(QStringLiteral("developerOverview"));
    m_overview->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_overview->setWordWrap(true);
    m_overview->setStyleSheet(QStringLiteral(
        "font-family: Consolas, monospace; font-size: 17px;"));
    layout->addWidget(m_overview, 1);
    auto* simulations = new QHBoxLayout;
    const QList<QPair<QString, DeveloperSimulation>> actions {
        {QStringLiteral("模拟“你好”"), DeveloperSimulation::Greeting},
        {QStringLiteral("模拟“知道了”"), DeveloperSimulation::Acknowledge},
        {QStringLiteral("模拟挥手"), DeveloperSimulation::Wave},
        {QStringLiteral("模拟提醒"), DeveloperSimulation::ReminderDue},
        {QStringLiteral("模拟紧急"), DeveloperSimulation::Emergency}
    };
    for (const auto& action : actions) {
        auto* button = actionButton(action.first, page);
        simulations->addWidget(button);
        connect(button, &QPushButton::clicked, this,
                [this, value = action.second] { emit simulationRequested(value); });
    }
    layout->addLayout(simulations);
    return page;
}

QWidget* DeveloperPage::createKwsTab()
{
    auto* page = new QWidget(this);
    auto* layout = new QGridLayout(page);
    layout->setContentsMargins(20, 10, 20, 12);
    layout->setHorizontalSpacing(12);
    layout->setVerticalSpacing(6);
    m_kwsState = new QLabel(page);
    m_kwsState->setWordWrap(true);
    m_kwsMetrics = new QLabel(page);
    m_kwsLast = new QLabel(page);
    m_kwsEvents = new QListWidget(page);
    m_kwsEvents->setMaximumHeight(70);
    m_rms = new QProgressBar(page);
    m_peak = new QProgressBar(page);
    for (QProgressBar* bar : {m_rms, m_peak}) {
        bar->setRange(0, 1000);
        bar->setTextVisible(true);
    }
    m_audioDevice = new QComboBox(page);
    m_sampleRate = new QSpinBox(page);
    m_sampleRate->setRange(8'000, 192'000);
    m_channels = new QSpinBox(page);
    m_channels->setRange(1, 8);
    m_micChannel = new QSpinBox(page);
    m_micChannel->setRange(0, 7);
    connect(m_channels, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int channels) {
        m_micChannel->setMaximum(qMax(0, channels - 1));
    });
    m_threshold = new QDoubleSpinBox(page);
    m_threshold->setRange(0.01, 1.0);
    m_threshold->setSingleStep(0.01);
    m_score = new QDoubleSpinBox(page);
    m_score->setRange(0.1, 10.0);
    m_score->setSingleStep(0.1);
    m_kwsEnabled = new QCheckBox(QStringLiteral("Enabled"), page);

    layout->addWidget(m_kwsState, 0, 0, 1, 4);
    layout->addWidget(new QLabel(QStringLiteral("Input Device"), page), 1, 0);
    layout->addWidget(m_audioDevice, 1, 1, 1, 3);
    layout->addWidget(new QLabel(QStringLiteral("RMS"), page), 2, 0);
    layout->addWidget(m_rms, 2, 1);
    layout->addWidget(new QLabel(QStringLiteral("Peak"), page), 2, 2);
    layout->addWidget(m_peak, 2, 3);
    layout->addWidget(new QLabel(QStringLiteral("Sample rate"), page), 3, 0);
    layout->addWidget(m_sampleRate, 3, 1);
    layout->addWidget(new QLabel(QStringLiteral("Channels"), page), 3, 2);
    layout->addWidget(m_channels, 3, 3);
    layout->addWidget(new QLabel(QStringLiteral("Mic channel"), page), 4, 0);
    layout->addWidget(m_micChannel, 4, 1);
    layout->addWidget(new QLabel(QStringLiteral("Threshold / Score"), page), 4, 2);
    auto* tuning = new QHBoxLayout;
    tuning->addWidget(m_threshold);
    tuning->addWidget(m_score);
    layout->addLayout(tuning, 4, 3);
    layout->addWidget(m_kwsMetrics, 5, 0, 1, 4);
    layout->addWidget(m_kwsLast, 6, 0, 1, 4);
    layout->addWidget(m_kwsEvents, 7, 0, 1, 4);
    auto* controls = new QHBoxLayout;
    controls->addWidget(m_kwsEnabled);
    auto* start = actionButton(QStringLiteral("Start"), page);
    auto* stop = actionButton(QStringLiteral("Stop"), page);
    auto* restart = actionButton(QStringLiteral("Restart"), page);
    auto* apply = actionButton(QStringLiteral("应用并重启"), page);
    for (QPushButton* button : {start, stop, restart, apply})
        controls->addWidget(button);
    layout->addLayout(controls, 8, 0, 1, 4);
    connect(m_kwsEnabled, &QCheckBox::toggled,
            this, &DeveloperPage::kwsEnableRequested);
    connect(start, &QPushButton::clicked, this, &DeveloperPage::kwsStartRequested);
    connect(stop, &QPushButton::clicked, this, &DeveloperPage::kwsStopRequested);
    connect(restart, &QPushButton::clicked, this, &DeveloperPage::kwsRestartRequested);
    connect(apply, &QPushButton::clicked, this, [this] {
        KeywordSpottingConfig config;
        config.enabled = m_kwsEnabled->isChecked();
        config.audioDevice = m_audioDevice->currentData().toString();
        config.captureSampleRate = m_sampleRate->value();
        config.captureChannels = m_channels->value();
        config.microphoneChannel = m_micChannel->value();
        config.threshold = m_threshold->value();
        config.score = m_score->value();
        emit kwsReconfigureRequested(config);
    });
    return page;
}

QWidget* DeveloperPage::createVisionTab()
{
    auto* page = new QWidget(this);
    auto* layout = new QGridLayout(page);
    layout->setContentsMargins(20, 10, 20, 12);
    layout->setVerticalSpacing(8);
    m_visionState = new QLabel(page);
    m_visionState->setWordWrap(true);
    m_visionMetrics = new QLabel(page);
    m_visionLast = new QLabel(page);
    m_camera = new QComboBox(page);
    m_width = new QSpinBox(page);
    m_width->setRange(64, 1920);
    m_height = new QSpinBox(page);
    m_height->setRange(48, 1080);
    m_fps = new QSpinBox(page);
    m_fps->setRange(1, 30);
    m_wave = new QCheckBox(QStringLiteral("Wave recognition"), page);
    m_fall = new QCheckBox(QStringLiteral("Fall candidate (experimental)"), page);
    m_visionEnabled = new QCheckBox(QStringLiteral("Enabled"), page);
    layout->addWidget(m_visionState, 0, 0, 1, 4);
    layout->addWidget(new QLabel(QStringLiteral("Camera"), page), 1, 0);
    layout->addWidget(m_camera, 1, 1, 1, 3);
    layout->addWidget(new QLabel(QStringLiteral("Width"), page), 2, 0);
    layout->addWidget(m_width, 2, 1);
    layout->addWidget(new QLabel(QStringLiteral("Height"), page), 2, 2);
    layout->addWidget(m_height, 2, 3);
    layout->addWidget(new QLabel(QStringLiteral("Target FPS"), page), 3, 0);
    layout->addWidget(m_fps, 3, 1);
    layout->addWidget(m_wave, 3, 2);
    layout->addWidget(m_fall, 3, 3);
    layout->addWidget(m_visionMetrics, 4, 0, 1, 4);
    layout->addWidget(m_visionLast, 5, 0, 1, 4);
    auto* controls = new QHBoxLayout;
    controls->addWidget(m_visionEnabled);
    auto* start = actionButton(QStringLiteral("Start"), page);
    auto* stop = actionButton(QStringLiteral("Stop"), page);
    auto* restart = actionButton(QStringLiteral("Restart"), page);
    auto* apply = actionButton(QStringLiteral("应用并重启"), page);
    for (QPushButton* button : {start, stop, restart, apply})
        controls->addWidget(button);
    layout->addLayout(controls, 6, 0, 1, 4);
    connect(m_visionEnabled, &QCheckBox::toggled,
            this, &DeveloperPage::visionEnableRequested);
    connect(start, &QPushButton::clicked, this, &DeveloperPage::visionStartRequested);
    connect(stop, &QPushButton::clicked, this, &DeveloperPage::visionStopRequested);
    connect(restart, &QPushButton::clicked, this, &DeveloperPage::visionRestartRequested);
    connect(apply, &QPushButton::clicked, this, [this] {
        VisionConfig config;
        config.enabled = m_visionEnabled->isChecked();
        config.cameraIndex = m_camera->currentData().toInt();
        config.frameWidth = m_width->value();
        config.frameHeight = m_height->value();
        config.targetFps = m_fps->value();
        config.waveEnabled = m_wave->isChecked();
        config.fallCandidateEnabled = m_fall->isChecked();
        emit visionReconfigureRequested(config);
    });
    return page;
}

QWidget* DeveloperPage::createDevicesTab()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    m_devices = new QTextEdit(page);
    m_devices->setReadOnly(true);
    layout->addWidget(m_devices);
    return page;
}

QWidget* DeveloperPage::createEventsTab()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    m_events = new QListWidget(page);
    m_events->setObjectName(QStringLiteral("diagnosticEventList"));
    layout->addWidget(m_events);
    return page;
}

void DeveloperPage::setSnapshot(const DeveloperSnapshot& snapshot)
{
    m_snapshot = snapshot;
    updateDeviceChoices(snapshot);
    const auto& kws = snapshot.kws;
    const auto& vision = snapshot.vision;
    m_overview->setText(QStringLiteral(
        "KWS     Enabled=%1 Available=%2 Running=%3 State=%4 PID=%5\n"
        "Vision  Enabled=%6 Available=%7 Running=%8 State=%9 PID=%10\n"
        "Audio   Available=%11 inputs=%12\nCamera  Available=%13 devices=%14\n"
        "SQLite  Available=%15 Schema=V%16  %17\nNetwork Available=%18  %19\n"
        "Power   Available=%20  %21\n"
        "Remote AI: Not implemented    Motion MCU: Not implemented")
        .arg(yesNo(kws.enabled), yesNo(kws.available), yesNo(kws.running),
             kwsStateName(kws.state)).arg(kws.workerPid)
        .arg(yesNo(vision.enabled), yesNo(vision.available), yesNo(vision.running),
             visionStateName(vision.state)).arg(vision.workerPid)
        .arg(yesNo(!snapshot.audioInputs.isEmpty())).arg(snapshot.audioInputs.size())
        .arg(yesNo(!snapshot.cameras.isEmpty())).arg(snapshot.cameras.size())
        .arg(yesNo(snapshot.sqliteAvailable)).arg(snapshot.sqliteSchemaVersion)
        .arg(snapshot.sqliteDetail)
        .arg(yesNo(snapshot.system.networkAvailable), snapshot.system.networkSummary)
        .arg(yesNo(snapshot.system.batteryPercent >= 0),
             snapshot.device.powerSummary));
    m_kwsState->setText(QStringLiteral(
        "Enabled=%1  Available=%2  Running=%3  State=%4  PID=%5  Uptime=%6\n%7")
        .arg(yesNo(kws.enabled), yesNo(kws.available), yesNo(kws.running),
             kwsStateName(kws.state)).arg(kws.workerPid).arg(uptimeText(kws.startedAt),
             kws.errorDetail.isEmpty() ? kws.summary : kws.errorDetail));
    setAudioLevel(kws.inputRms, kws.inputPeak);
    m_kwsMetrics->setText(QStringLiteral(
        "Decode=%1 ms  RTF=%2  Keyword latency=%3 ms  Dropped=%4")
        .arg(kws.lastDecodeElapsedMs, 0, 'f', 1).arg(kws.lastRtf, 0, 'f', 2)
        .arg(kws.lastKeywordLatencyMs, 0, 'f', 1).arg(kws.droppedUtterances));
    m_kwsLast->setText(QStringLiteral("Last keyword: %1    Last event: %2")
        .arg(kws.lastKeyword.isEmpty() ? QStringLiteral("--") : kws.lastKeyword,
             kws.lastDetectedAt.isValid() ? kws.lastDetectedAt.toString(QStringLiteral("HH:mm:ss"))
                                          : QStringLiteral("--")));
    if (!m_kwsControlsInitialized) {
        const auto blockKws = QSignalBlocker(m_kwsEnabled);
        m_kwsEnabled->setChecked(kws.enabled);
        m_sampleRate->setValue(kws.captureSampleRate);
        m_channels->setValue(kws.captureChannels);
        m_micChannel->setMaximum(qMax(0, kws.captureChannels - 1));
        m_micChannel->setValue(kws.microphoneChannel);
        m_threshold->setValue(kws.threshold);
        m_score->setValue(kws.score);
        m_kwsControlsInitialized = true;
    }

    m_visionState->setText(QStringLiteral(
        "Enabled=%1  Available=%2  Running=%3  CameraAvailable=%4  State=%5  PID=%6  Uptime=%7\n%8")
        .arg(yesNo(vision.enabled), yesNo(vision.available), yesNo(vision.running),
             yesNo(vision.cameraAvailable), visionStateName(vision.state))
        .arg(vision.workerPid).arg(uptimeText(vision.startedAt),
             vision.errorDetail.isEmpty() ? vision.summary : vision.errorDetail));
    m_visionMetrics->setText(QStringLiteral("Actual FPS=%1  Frame time=%2 ms  Target=%3 FPS")
        .arg(vision.effectiveFps, 0, 'f', 2).arg(vision.frameTimeMs, 0, 'f', 2)
        .arg(vision.targetFps));
    m_visionLast->setText(QStringLiteral("Last raw event=%1  confidence=%2  time=%3")
        .arg(visionEventName(vision.lastEventType)).arg(vision.lastConfidence, 0, 'f', 2)
        .arg(vision.lastEventAt.isValid() ? vision.lastEventAt.toString(QStringLiteral("HH:mm:ss"))
                                         : QStringLiteral("--")));
    if (!m_visionControlsInitialized) {
        const auto blockVision = QSignalBlocker(m_visionEnabled);
        m_visionEnabled->setChecked(vision.enabled);
        m_width->setValue(vision.frameWidth);
        m_height->setValue(vision.frameHeight);
        m_fps->setValue(vision.targetFps);
        m_wave->setChecked(vision.waveEnabled);
        m_fall->setChecked(vision.fallCandidateEnabled);
        m_visionControlsInitialized = true;
    }

    QStringList deviceLines {QStringLiteral("Audio inputs:")};
    for (const auto& device : snapshot.audioInputs)
        deviceLines << QStringLiteral("  %1 | %2 | available=%3")
                           .arg(device.id, device.displayName, yesNo(device.available));
    deviceLines << QStringLiteral("\nCameras:");
    for (const auto& camera : snapshot.cameras)
        deviceLines << QStringLiteral("  %1 | index=%2 | available=%3")
                           .arg(camera.id).arg(camera.index).arg(yesNo(camera.available));
    deviceLines << QStringLiteral("\nSQLite: %1 (schema V%2)")
                       .arg(snapshot.sqliteDetail).arg(snapshot.sqliteSchemaVersion);
    m_devices->setPlainText(deviceLines.join(QLatin1Char('\n')));
}

void DeveloperPage::updateDeviceChoices(const DeveloperSnapshot& snapshot)
{
    const QString selectedAudio = m_audioDevice->currentData().toString();
    QStringList currentAudio;
    for (int i = 0; i < m_audioDevice->count(); ++i)
        currentAudio << m_audioDevice->itemData(i).toString();
    QStringList nextAudio;
    for (const auto& device : snapshot.audioInputs)
        nextAudio << device.id;
    if (currentAudio != nextAudio) {
        m_audioDevice->clear();
        for (const auto& device : snapshot.audioInputs)
            m_audioDevice->addItem(device.displayName, device.id);
    }
    QString audioTarget = selectedAudio.isEmpty() ? snapshot.kws.audioDevice : selectedAudio;
    const int audioIndex = m_audioDevice->findData(audioTarget);
    if (audioIndex >= 0)
        m_audioDevice->setCurrentIndex(audioIndex);

    const bool hadCameraSelection = m_camera->count() > 0;
    const int selectedCamera = m_camera->currentData().toInt();
    QList<int> currentCameras;
    for (int i = 0; i < m_camera->count(); ++i)
        currentCameras << m_camera->itemData(i).toInt();
    QList<int> nextCameras;
    for (const auto& camera : snapshot.cameras)
        nextCameras << camera.index;
    if (currentCameras != nextCameras) {
        m_camera->clear();
        for (const auto& camera : snapshot.cameras)
            m_camera->addItem(camera.displayName, camera.index);
    }
    const int cameraTarget = hadCameraSelection && selectedCamera >= 0
        ? selectedCamera : snapshot.vision.cameraIndex;
    const int cameraIndex = m_camera->findData(cameraTarget);
    if (cameraIndex >= 0)
        m_camera->setCurrentIndex(cameraIndex);
}

void DeveloperPage::setEvents(const QList<DiagnosticEvent>& events)
{
    m_events->clear();
    m_kwsEvents->clear();
    for (const DiagnosticEvent& event : events)
        appendEvent(event);
}

void DeveloperPage::appendEvent(const DiagnosticEvent& event)
{
    while (m_events->count() >= 200)
        delete m_events->takeItem(0);
    m_events->addItem(eventText(event));
    m_events->scrollToBottom();
    if (event.source == DiagnosticSource::KwsAdapter
        || event.source == DiagnosticSource::KwsService
        || event.source == DiagnosticSource::Audio) {
        while (m_kwsEvents->count() >= 8)
            delete m_kwsEvents->takeItem(0);
        m_kwsEvents->addItem(eventText(event));
        m_kwsEvents->scrollToBottom();
    }
}

void DeveloperPage::setAudioLevel(double rms, double peak)
{
    m_rms->setValue(qBound(0, qRound(rms * 1000.0), 1000));
    m_peak->setValue(qBound(0, qRound(peak * 1000.0), 1000));
    m_rms->setFormat(QStringLiteral("%1").arg(rms, 0, 'f', 4));
    m_peak->setFormat(QStringLiteral("%1").arg(peak, 0, 'f', 4));
}

QString DeveloperPage::eventText(const DiagnosticEvent& event)
{
    return QStringLiteral("%1 [%2/%3] %4 %5")
        .arg(event.timestamp.toString(QStringLiteral("HH:mm:ss.zzz")))
        .arg(sourceName(event.source))
        .arg(levelName(event.level))
        .arg(event.event, event.detail);
}
