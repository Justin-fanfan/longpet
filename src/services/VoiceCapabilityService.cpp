#include "VoiceCapabilityService.h"

#include "services/SystemService.h"

VoiceCapabilityService::VoiceCapabilityService(
    const AiConfiguration& configuration, SystemService* systemService,
    QObject* parent)
    : QObject(parent), m_configuration(configuration),
      m_systemService(systemService)
{
    m_retryTimer.setSingleShot(true);
    m_retryTimer.setInterval(qMax(1'000, configuration.voice.availabilityRetryMs));
    connect(&m_retryTimer, &QTimer::timeout, this, [this] {
        m_providerDegraded = false;
        m_providerReason.clear();
        reevaluate();
    });
    if (m_systemService) {
        connect(m_systemService, &SystemService::statusChanged,
                this, [this](const SystemStatus&) { reevaluate(); });
    }
    reevaluate();
}

bool VoiceCapabilityService::onlineAiAvailable() const
{
    return m_available;
}

QString VoiceCapabilityService::unavailableReason() const
{
    return m_reason;
}

void VoiceCapabilityService::reportProviderAvailability(
    bool available, const QString& reason)
{
    if (!available) {
        m_providerDegraded = true;
        m_retryTimer.start();
        m_providerReason = reason.simplified();
        reevaluate();
        return;
    }
    m_providerDegraded = false;
    m_providerReason.clear();
    m_retryTimer.stop();
    reevaluate();
}

void VoiceCapabilityService::reevaluate()
{
    bool available = true;
    QString reason;
    const QString configurationError = m_configuration.validationError();
    if (!configurationError.isEmpty()) {
        available = false;
        reason = QStringLiteral("联网语音配置不完整");
    } else if (!m_systemService || !m_systemService->status().networkKnown) {
        available = false;
        reason = QStringLiteral("网络状态尚未就绪");
    } else if (!m_systemService->status().networkAvailable) {
        available = false;
        reason = QStringLiteral("设备当前未联网");
    } else if (m_providerDegraded) {
        available = false;
        reason = m_providerReason.isEmpty()
            ? QStringLiteral("联网语音服务暂时不可用") : m_providerReason;
    }
    if (available == m_available && reason == m_reason)
        return;
    m_available = available;
    m_reason = reason;
    emit availabilityChanged(m_available, m_reason);
}
