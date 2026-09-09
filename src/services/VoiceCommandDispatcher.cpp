#include "VoiceCommandDispatcher.h"
#include "services/KwsPorts.h"
#include "services/LocalCompanionService.h"
#include "services/VoiceCapabilityService.h"
#include "services/VoiceInteractionService.h"
#include <QDateTime>
#include <QDebug>

VoiceCommandDispatcher::VoiceCommandDispatcher(
    const KwsConfiguration& configuration, KwsPort* kws,
    VoiceCapabilityService* capability, VoiceInteractionService* voice,
    LocalCompanionService* companion, QObject* parent)
    : QObject(parent), m_configuration(configuration), m_kws(kws),
      m_capability(capability), m_voice(voice), m_companion(companion)
{
    m_commandWindow.setSingleShot(true);
    m_commandWindow.setInterval(qMax(1'000, configuration.commandTimeoutMs));
    connect(&m_commandWindow, &QTimer::timeout, this, [this] {
        m_offlineArmed = false;
        emit userMessage(QStringLiteral("离线语音：可以说“陪我说话”“打开提醒”“现在几点”"));
    });
    if (m_kws) {
        connect(m_kws, &KwsPort::keywordDetected, this, &VoiceCommandDispatcher::handleKeyword);
        connect(m_kws, &KwsPort::kwsError, this,
                [this](const QString& message, const QString& diagnostic) {
            qWarning().noquote() << "KWS error:" << diagnostic;
            emit userMessage(message + QStringLiteral("，仍可点击按钮使用语音"));
        });
    }
    if (m_companion) {
        connect(m_companion, &LocalCompanionService::activityChanged, this,
                [this](bool active) {
            emit companionActivityChanged(active);
            if (!active && m_started && m_pending != PendingAction::None)
                performPending();
        });
        connect(m_companion, &LocalCompanionService::playbackFailed, this,
                [this](const QString& message, const QString& diagnostic) {
            qWarning().noquote() << "Offline companion failed:" << diagnostic;
            emit userMessage(message);
        });
    }
}

void VoiceCommandDispatcher::start()
{
    m_started = true;
    if (m_kws && m_configuration.enabled)
        m_kws->start();
}

void VoiceCommandDispatcher::stop()
{
    m_started = false;
    requestCancelInteraction();
    if (m_kws)
        m_kws->stop();
}

void VoiceCommandDispatcher::requestStartInteraction()
{
    if (m_externalMediaActive) {
        emit userMessage(QStringLiteral("正在通话，请先挂断"));
        return;
    }
    if (m_capability && !m_capability->onlineAiAvailable()) {
        emit userMessage(m_capability->unavailableReason()
                         + QStringLiteral("，正在手动重试"));
    }
    prepare(PendingAction::StartVoice);
}

void VoiceCommandDispatcher::requestRestartInteraction()
{
    if (!m_externalMediaActive)
        prepare(PendingAction::RestartVoice);
}

void VoiceCommandDispatcher::clearCommandWindow()
{
    m_offlineArmed = false;
    m_commandWindow.stop();
}

void VoiceCommandDispatcher::requestCancelInteraction()
{
    m_pending = PendingAction::None;
    clearCommandWindow();
    if (m_voice)
        m_voice->cancelInteraction();
    if (m_companion)
        m_companion->stop();
}

void VoiceCommandDispatcher::notifyExternalMediaActivity(bool active)
{
    m_externalMediaActive = active;
    if (active)
        clearCommandWindow();
}

void VoiceCommandDispatcher::handleKeyword(const KwsEvent& event)
{
    if (!m_started)
        return;
    qInfo().noquote() << QStringLiteral("KWS keyword=%1 score=%2 timestamp_ms=%3")
        .arg(event.keyword).arg(event.score, 0, 'f', 4).arg(event.timestampMs);
    // Local high-priority event contract, not full-duplex acoustic interruption.
    if (event.keyword == QStringLiteral("救命")) {
        requestCancelInteraction();
        emit emergencyRequested();
        return;
    }
    if (event.keyword == QStringLiteral("停止")) {
        requestCancelInteraction();
        emit userMessage(QStringLiteral("已停止当前语音操作"));
        return;
    }
    if (event.keyword == QStringLiteral("你好") || m_emergencyActive || mediaBusy())
        return;
    const bool online = m_capability && m_capability->onlineAiAvailable();
    if (event.keyword == QStringLiteral("小龙小龙")) {
        if (online) {
            prepare(PendingAction::StartVoice);
        } else {
            m_offlineArmed = true;
            m_commandWindow.start();
            emit userMessage(QStringLiteral("离线语音：请说“陪我说话”“打开提醒”或“现在几点”"));
        }
        return;
    }
    // Offline shortcuts also work directly. Online natural language belongs to LLM.
    if (online)
        return;
    clearCommandWindow();
    if (event.keyword == QStringLiteral("陪我说话"))
        prepare(PendingAction::PlayCompanion);
    else if (event.keyword == QStringLiteral("打开提醒"))
        emit remindersRequested();
    else if (event.keyword == QStringLiteral("现在几点"))
        emit localTimeRequested(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm")));
    else if (event.keyword == QStringLiteral("联系家人"))
        emit familyContactRequested();
    else if (event.keyword == QStringLiteral("返回主页"))
        emit homeRequested();
    else if (event.keyword == QStringLiteral("音量大点"))
        emit volumeDeltaRequested(10);
    else if (event.keyword == QStringLiteral("音量小点"))
        emit volumeDeltaRequested(-10);
}

void VoiceCommandDispatcher::prepare(PendingAction action)
{
    clearCommandWindow();
    m_pending = action;
    if (m_companion && m_companion->isActive()) {
        m_companion->stop();
        return;
    }
    performPending();
}

void VoiceCommandDispatcher::performPending()
{
    const PendingAction action = m_pending;
    m_pending = PendingAction::None;
    if (action == PendingAction::None)
        return;
    if (action == PendingAction::PlayCompanion) {
        QString error;
        if (!m_companion || !m_companion->start(&error))
            emit userMessage(error.isEmpty() ? QStringLiteral("离线陪伴暂时不可用") : error);
        return;
    }
    if (!m_voice) {
        emit userMessage(QStringLiteral("语音交互服务不可用"));
        return;
    }
    const VoiceInteractionResult result = action == PendingAction::RestartVoice
        ? m_voice->restartInteraction() : m_voice->startInteraction();
    if (!result.success)
        emit userMessage(result.error);
}

bool VoiceCommandDispatcher::mediaBusy() const
{
    return m_externalMediaActive || m_pending != PendingAction::None
        || (m_voice && m_voice->mediaActive())
        || (m_companion && m_companion->isActive());
}
