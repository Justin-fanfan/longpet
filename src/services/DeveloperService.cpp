#include "DeveloperService.h"

#include "platform/AudioDeviceAdapter.h"
#include "platform/CameraDeviceAdapter.h"
#include "services/DiagnosticsService.h"
#include "services/KeywordSpottingService.h"
#include "services/ReminderService.h"
#include "services/SystemService.h"
#include "services/VisionService.h"

namespace {
QString semanticName(KeywordSemantic semantic)
{
    switch (semantic) {
    case KeywordSemantic::Greeting: return QStringLiteral("GREETING");
    case KeywordSemantic::Emergency: return QStringLiteral("EMERGENCY");
    case KeywordSemantic::Stop: return QStringLiteral("STOP");
    case KeywordSemantic::Acknowledge: return QStringLiteral("ACKNOWLEDGE");
    case KeywordSemantic::Complete: return QStringLiteral("COMPLETE");
    case KeywordSemantic::Unknown: return QStringLiteral("UNKNOWN");
    }
    return QStringLiteral("UNKNOWN");
}

QString visionName(VisionEventType type)
{
    switch (type) {
    case VisionEventType::Wave: return QStringLiteral("Wave");
    case VisionEventType::FallCandidate: return QStringLiteral("Fall candidate");
    case VisionEventType::FallConfirmed: return QStringLiteral("Fall confirmed");
    case VisionEventType::PersonDetected: return QStringLiteral("Person (reserved)");
    case VisionEventType::Unknown: return QStringLiteral("Unknown");
    }
    return QStringLiteral("Unknown");
}
}

DeveloperService::DeveloperService(KeywordSpottingService* kws,
                                   VisionService* vision,
                                   ReminderService* reminders,
                                   SystemService* system,
                                   AudioDeviceAdapter* audioDevices,
                                   CameraDeviceAdapter* cameraDevices,
                                   DiagnosticsService* diagnostics,
                                   BoolProbe databaseAvailable,
                                   IntProbe schemaVersion,
                                   StringProbe databaseDetail,
                                   QObject* parent)
    : QObject(parent),
      m_kws(kws),
      m_vision(vision),
      m_reminders(reminders),
      m_system(system),
      m_audioDevices(audioDevices),
      m_cameraDevices(cameraDevices),
      m_diagnostics(diagnostics),
      m_databaseAvailable(std::move(databaseAvailable)),
      m_schemaVersion(std::move(schemaVersion)),
      m_databaseDetail(std::move(databaseDetail))
{
    m_snapshotTimer.setInterval(1'000);
    m_snapshotTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_snapshotTimer, &QTimer::timeout, this,
            [this] { emit snapshotChanged(snapshot()); });
    connectDiagnostics();
    const DeveloperSnapshot initial = snapshot();
    record(DiagnosticSource::Database,
           initial.sqliteAvailable ? DiagnosticLevel::Info : DiagnosticLevel::Warning,
           initial.sqliteAvailable ? QStringLiteral("SQLite available")
                                   : QStringLiteral("SQLite unavailable"),
           QStringLiteral("schema=V%1 %2")
               .arg(initial.sqliteSchemaVersion).arg(initial.sqliteDetail));
    m_snapshotTimer.start();
}

DeveloperSnapshot DeveloperService::snapshot() const
{
    DeveloperSnapshot value;
    if (m_kws)
        value.kws = m_kws->status();
    if (m_vision)
        value.vision = m_vision->status();
    if (m_system)
        value.system = m_system->status();
    if (m_system)
        value.device = m_system->deviceSummary();
    if (m_audioDevices)
        value.audioInputs = m_audioDevices->inputDevices();
    if (m_cameraDevices)
        value.cameras = m_cameraDevices->cameras();
    value.sqliteAvailable = m_databaseAvailable && m_databaseAvailable();
    value.sqliteSchemaVersion = m_schemaVersion ? m_schemaVersion() : 0;
    value.sqliteDetail = m_databaseDetail ? m_databaseDetail() : QString();
    value.capturedAt = QDateTime::currentDateTime();
    return value;
}

QList<DiagnosticEvent> DeveloperService::events() const
{
    return m_diagnostics ? m_diagnostics->events() : QList<DiagnosticEvent>();
}

bool DeveloperService::setKwsEnabled(bool enabled)
{
    const bool ok = m_kws && m_kws->setEnabled(enabled);
    record(DiagnosticSource::KwsService, ok ? DiagnosticLevel::Info : DiagnosticLevel::Error,
           enabled ? QStringLiteral("KWS enabled") : QStringLiteral("KWS disabled"));
    return ok;
}

bool DeveloperService::startKws()
{
    const bool ok = m_kws && m_kws->start();
    record(DiagnosticSource::KwsService, ok ? DiagnosticLevel::Info : DiagnosticLevel::Error,
           QStringLiteral("KWS start requested"));
    return ok;
}

void DeveloperService::stopKws()
{
    if (m_kws)
        m_kws->stop();
    record(DiagnosticSource::KwsService, DiagnosticLevel::Info,
           QStringLiteral("KWS stop requested"));
}

bool DeveloperService::restartKws()
{
    const bool ok = m_kws && m_kws->restart();
    record(DiagnosticSource::KwsService, ok ? DiagnosticLevel::Info : DiagnosticLevel::Error,
           QStringLiteral("KWS restart requested"));
    return ok;
}

bool DeveloperService::reconfigureKws(const KeywordSpottingConfig& config,
                                      QString* error)
{
    if (!m_audioDevices || !m_audioDevices->isInputAvailable(config.audioDevice)) {
        if (error)
            *error = QStringLiteral("所选音频输入设备不可用：%1").arg(config.audioDevice);
        record(DiagnosticSource::Audio, DiagnosticLevel::Warning,
               QStringLiteral("KWS reconfigure rejected"),
               error ? *error : config.audioDevice);
        return false;
    }
    const bool ok = m_kws && m_kws->reconfigure(config, error);
    record(DiagnosticSource::KwsService, ok ? DiagnosticLevel::Info : DiagnosticLevel::Error,
           QStringLiteral("KWS reconfigured"), error ? *error : QString());
    return ok;
}

bool DeveloperService::setVisionEnabled(bool enabled)
{
    const bool ok = m_vision && m_vision->setEnabled(enabled);
    record(DiagnosticSource::VisionService, ok ? DiagnosticLevel::Info : DiagnosticLevel::Error,
           enabled ? QStringLiteral("Vision enabled") : QStringLiteral("Vision disabled"));
    return ok;
}

bool DeveloperService::startVision()
{
    const bool ok = m_vision && m_vision->start();
    record(DiagnosticSource::VisionService, ok ? DiagnosticLevel::Info : DiagnosticLevel::Error,
           QStringLiteral("Vision start requested"));
    return ok;
}

void DeveloperService::stopVision()
{
    if (m_vision)
        m_vision->stop();
    record(DiagnosticSource::VisionService, DiagnosticLevel::Info,
           QStringLiteral("Vision stop requested"));
}

bool DeveloperService::restartVision()
{
    const bool ok = m_vision && m_vision->restart();
    record(DiagnosticSource::VisionService, ok ? DiagnosticLevel::Info : DiagnosticLevel::Error,
           QStringLiteral("Vision restart requested"));
    return ok;
}

bool DeveloperService::reconfigureVision(const VisionConfig& config,
                                         QString* error)
{
    if (!m_cameraDevices || !m_cameraDevices->isCameraAvailable(config.cameraIndex)) {
        if (error)
            *error = QStringLiteral("所选摄像头不可用：/dev/video%1")
                         .arg(config.cameraIndex);
        record(DiagnosticSource::VisionAdapter, DiagnosticLevel::Warning,
               QStringLiteral("Vision reconfigure rejected"),
               error ? *error : QString());
        return false;
    }
    const bool ok = m_vision && m_vision->reconfigure(config, error);
    record(DiagnosticSource::VisionService, ok ? DiagnosticLevel::Info : DiagnosticLevel::Error,
           QStringLiteral("Vision reconfigured"), error ? *error : QString());
    return ok;
}

void DeveloperService::simulateGreeting()
{
    record(DiagnosticSource::KwsService, DiagnosticLevel::Info,
           QStringLiteral("Simulate GREETING"));
    if (m_kws)
        m_kws->injectDiagnosticSemantic(KeywordSemantic::Greeting,
                                        QStringLiteral("你好（模拟）"));
}

void DeveloperService::simulateAcknowledge()
{
    record(DiagnosticSource::KwsService, DiagnosticLevel::Info,
           QStringLiteral("Simulate ACKNOWLEDGE"));
    if (m_kws)
        m_kws->injectDiagnosticSemantic(KeywordSemantic::Acknowledge,
                                        QStringLiteral("知道了（模拟）"));
}

void DeveloperService::simulateWave()
{
    record(DiagnosticSource::VisionService, DiagnosticLevel::Info,
           QStringLiteral("Simulate Wave"));
    if (m_vision)
        m_vision->injectDiagnosticDetection(VisionEventType::Wave, 1.0);
}

void DeveloperService::simulateReminderDue()
{
    record(DiagnosticSource::Reminder, DiagnosticLevel::Info,
           QStringLiteral("Simulate Reminder Due"));
    if (m_reminders)
        m_reminders->requestDiagnosticPresentation();
}

void DeveloperService::simulateEmergency()
{
    record(DiagnosticSource::KwsService, DiagnosticLevel::Warning,
           QStringLiteral("Simulate Emergency"));
    if (m_kws)
        m_kws->injectDiagnosticSemantic(KeywordSemantic::Emergency,
                                        QStringLiteral("救命（模拟）"));
}

void DeveloperService::recordControllerEvent(const QString& event,
                                             const QString& detail,
                                             DiagnosticLevel level)
{
    record(DiagnosticSource::Controller, level, event, detail);
}

void DeveloperService::connectDiagnostics()
{
    if (m_diagnostics) {
        connect(m_diagnostics, &DiagnosticsService::eventAdded,
                this, &DeveloperService::diagnosticEventAdded);
    }
    if (m_kws) {
        connect(m_kws, &KeywordSpottingService::statusChanged, this,
                [this](const KeywordSpottingStatus& status) {
            emit audioLevelChanged(status.inputRms, status.inputPeak);
            const int state = static_cast<int>(status.state);
            if (state == m_lastKwsState)
                return;
            m_lastKwsState = state;
            record(DiagnosticSource::KwsService,
                   status.state == KeywordSpottingRuntimeState::Error
                       ? DiagnosticLevel::Error : DiagnosticLevel::Info,
                   QStringLiteral("KWS state changed"), status.summary);
        });
        connect(m_kws, &KeywordSpottingService::keywordDetected, this,
                [this](const KeywordDetection& detection) {
            record(DiagnosticSource::KwsAdapter, DiagnosticLevel::Info,
                   QStringLiteral("keyword=%1").arg(detection.keyword),
                   detection.signal);
        });
        connect(m_kws, &KeywordSpottingService::semanticDetected, this,
                [this](KeywordSemantic semantic, const QString& keyword) {
            record(DiagnosticSource::KwsService, DiagnosticLevel::Info,
                   QStringLiteral("semantic=%1").arg(semanticName(semantic)), keyword);
        });
        connect(m_kws, &KeywordSpottingService::adapterDiagnostic, this,
                [this](const QString& message) {
            record(DiagnosticSource::KwsAdapter, DiagnosticLevel::Warning,
                   QStringLiteral("worker diagnostic"), message);
        });
        connect(m_kws, &KeywordSpottingService::recoveryScheduled, this,
                [this](int attempt, int delayMs) {
            record(DiagnosticSource::KwsAdapter, DiagnosticLevel::Warning,
                   QStringLiteral("worker retry scheduled"),
                   QStringLiteral("attempt=%1 delay=%2ms").arg(attempt).arg(delayMs));
        });
    }
    if (m_vision) {
        connect(m_vision, &VisionService::statusChanged, this,
                [this](const VisionStatus& status) {
            const int state = static_cast<int>(status.state);
            if (state == m_lastVisionState)
                return;
            m_lastVisionState = state;
            record(DiagnosticSource::VisionService,
                   status.state == VisionRuntimeState::Error
                       ? DiagnosticLevel::Error : DiagnosticLevel::Info,
                   QStringLiteral("Vision state changed"), status.summary);
        });
        connect(m_vision, &VisionService::detectionAccepted, this,
                [this](const VisionDetection& detection) {
            record(DiagnosticSource::VisionService, DiagnosticLevel::Info,
                   visionName(detection.type),
                   QStringLiteral("confidence=%1 source=%2")
                       .arg(detection.confidence, 0, 'f', 2).arg(detection.source));
        });
        connect(m_vision, &VisionService::detectionSuppressed, this,
                [this](const VisionDetection& detection, const QString& reason) {
            record(DiagnosticSource::VisionService, DiagnosticLevel::Debug,
                   QStringLiteral("%1 suppressed").arg(visionName(detection.type)), reason);
        });
        connect(m_vision, &VisionService::adapterDiagnostic, this,
                [this](const QString& message) {
            record(DiagnosticSource::VisionAdapter, DiagnosticLevel::Warning,
                   QStringLiteral("worker diagnostic"), message);
        });
        connect(m_vision, &VisionService::recoveryScheduled, this,
                [this](int attempt, int delayMs) {
            record(DiagnosticSource::VisionAdapter, DiagnosticLevel::Warning,
                   QStringLiteral("worker retry scheduled"),
                   QStringLiteral("attempt=%1 delay=%2ms").arg(attempt).arg(delayMs));
        });
    }
    if (m_reminders) {
        connect(m_reminders, &ReminderService::reminderPresentationRequested,
                this, [this](const ReminderPresentation& presentation) {
            record(DiagnosticSource::Reminder, DiagnosticLevel::Info,
                   QStringLiteral("presentation requested"),
                   QStringLiteral("event=%1 count=%2 title=%3")
                       .arg(presentation.occurrence.id)
                       .arg(presentation.occurrence.presentationCount)
                       .arg(presentation.reminder.title));
        });
        connect(m_reminders, &ReminderService::errorOccurred, this,
                [this](const QString& error) {
            record(DiagnosticSource::Reminder, DiagnosticLevel::Error,
                   QStringLiteral("scheduler error"), error);
        });
    }
    if (m_system) {
        connect(m_system, &SystemService::statusChanged, this,
                [this](const SystemStatus& status) {
            if (status.networkKnown == m_lastNetworkKnown
                && status.networkAvailable == m_lastNetworkAvailable
                && status.networkSummary == m_lastNetworkSummary) {
                return;
            }
            m_lastNetworkKnown = status.networkKnown;
            m_lastNetworkAvailable = status.networkAvailable;
            m_lastNetworkSummary = status.networkSummary;
            record(DiagnosticSource::System, DiagnosticLevel::Info,
                   QStringLiteral("network state changed"), status.networkSummary);
        });
    }
}

void DeveloperService::record(DiagnosticSource source, DiagnosticLevel level,
                              const QString& event, const QString& detail)
{
    if (m_diagnostics)
        m_diagnostics->record(source, level, event, detail);
}
