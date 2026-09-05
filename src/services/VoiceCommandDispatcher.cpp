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
    m_pauseTimeout.setSingleShot(true);
    m_pauseTimeout.setInterval(qMax(100, configuration.pauseTimeoutMs));
    connect(&m_pauseTimeout, &QTimer::timeout, this, [this] {
        qWarning("KWS pause acknowledgement timed out; continuing with guarded media start");
        performPending();
    });
    m_resumeTimer.setSingleShot(true);
    m_resumeTimer.setInterval(qMax(0, configuration.resumeCooldownMs));
    connect(&m_resumeTimer, &QTimer::timeout, this, [this] {
        if (!mediaBusy() && m_kws) {
            m_ignoreKeywordsUntilMs = QDateTime::currentMSecsSinceEpoch()
                + qMax(300, m_configuration.resumeCooldownMs / 2);
            m_kws->resume();
        }
    });
    m_commandWindow.setSingleShot(true);
    m_commandWindow.setInterval(qMax(1'000, configuration.commandTimeoutMs));
    connect(&m_commandWindow, &QTimer::timeout, this, [this] {
        m_offlineArmed = false;
        emit userMessage(QStringLiteral("离线语音指令已超时，请重新说“小龙小龙”"));
    });

    if (m_kws) {
        connect(m_kws, &KwsPort::keywordDetected,
                this, &VoiceCommandDispatcher::handleKeyword);
        connect(m_kws, &KwsPort::kwsPaused, this, [this] {
            m_pauseTimeout.stop();
            performPending();
        });
        connect(m_kws, &KwsPort::kwsReady, this, [this] {
            qInfo("KWS ready");
        });
        connect(m_kws, &KwsPort::kwsError, this,
                [this](const QString& message, const QString& diagnostic) {
            qWarning().noquote() << "KWS error:" << diagnostic;
            emit userMessage(message + QStringLiteral("，仍可点击按钮使用语音"));
            if (m_pending != PendingAction::None) {
                m_pauseTimeout.stop();
                performPending();
            }
        });
    }
    if (m_voice) {
        connect(m_voice, &VoiceInteractionService::activityChanged,
                this, [this](bool active) {
            if (active) {
                m_resumeTimer.stop();
                if (m_kws && !m_kws->isPaused())
                    m_kws->pause();
            } else {
                scheduleKwsResume();
            }
        });
    }
    if (m_companion) {
        connect(m_companion, &LocalCompanionService::activityChanged,
                this, [this](bool active) {
            if (active) {
                m_resumeTimer.stop();
                if (m_kws && !m_kws->isPaused())
                    m_kws->pause();
            } else if (m_pending != PendingAction::None) {
                performPending();
            } else {
                scheduleKwsResume();
            }
        });
        connect(m_companion, &LocalCompanionService::playbackFailed,
                this, [this](const QString& message, const QString& diagnostic) {
            qWarning().noquote() << "Offline companion playback failed:" << diagnostic;
            emit userMessage(message);
        });
    }
}

void VoiceCommandDispatcher::start()
{
    if (m_kws && m_configuration.enabled)
        m_kws->start();
}

void VoiceCommandDispatcher::stop()
{
    m_pauseTimeout.stop();
    m_resumeTimer.stop();
    m_commandWindow.stop();
    m_pending = PendingAction::None;
    if (m_companion)
        m_companion->stop();
    if (m_voice)
        m_voice->cancelInteraction();
    if (m_kws)
        m_kws->stop();
}

void VoiceCommandDispatcher::requestStartInteraction()
{
    m_offlineArmed = false;
    m_commandWindow.stop();
    emit userMessage(QStringLiteral("正在准备麦克风"));
    prepare(PendingAction::StartVoice);
}

void VoiceCommandDispatcher::requestRestartInteraction()
{
    m_offlineArmed = false;
    m_commandWindow.stop();
    prepare(PendingAction::RestartVoice);
}

void VoiceCommandDispatcher::requestCancelInteraction()
{
    m_pending = PendingAction::None;
    m_offlineArmed = false;
    m_commandWindow.stop();
    m_pauseTimeout.stop();
    if (m_voice)
        m_voice->cancelInteraction();
    if (m_companion)
        m_companion->stop();
    scheduleKwsResume();
}

void VoiceCommandDispatcher::notifyExternalMediaActivity(bool active)
{
    if (active) {
        m_resumeTimer.stop();
        if (m_kws && !m_kws->isPaused())
            m_kws->pause();
        return;
    }
    scheduleKwsResume();
}

void VoiceCommandDispatcher::handleKeyword(const KwsEvent& event)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now < m_ignoreKeywordsUntilMs)
        return;
    qInfo().noquote() << QStringLiteral(
        "KWS keyword=%1 score=%2 timestamp_ms=%3")
            .arg(event.keyword).arg(event.score, 0, 'f', 4)
            .arg(event.timestampMs);

    if (event.keyword == QStringLiteral("你好")) {
        qInfo("KWS keyword ignored by policy: nihao");
        return;
    }
    if (event.keyword == QStringLiteral("救命")) {
        m_offlineArmed = false;
        m_commandWindow.stop();
        requestCancelInteraction();
        emit emergencyRequested();
        emit userMessage(QStringLiteral("已进入紧急求助页面"));
        return;
    }
    if (event.keyword == QStringLiteral("停止")) {
        requestCancelInteraction();
        emit userMessage(QStringLiteral("已停止当前语音操作"));
        return;
    }
    if (event.keyword == QStringLiteral("打开提醒")) {
        m_offlineArmed = false;
        m_commandWindow.stop();
        emit remindersRequested();
        return;
    }
    if (event.keyword == QStringLiteral("现在几点")) {
        m_offlineArmed = false;
        m_commandWindow.stop();
        const QString time = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm"));
        emit userMessage(QStringLiteral("现在是 %1").arg(time));
        return;
    }
    if (event.keyword == QStringLiteral("联系家人")) {
        m_offlineArmed = false;
        m_commandWindow.stop();
        prepare(PendingAction::ContactFamily);
        return;
    }
    if (event.keyword == QStringLiteral("返回主页")) {
        m_offlineArmed = false;
        m_commandWindow.stop();
        emit homeRequested();
        return;
    }
    if (event.keyword == QStringLiteral("音量大点")) {
        m_offlineArmed = false;
        m_commandWindow.stop();
        emit volumeDeltaRequested(10);
        return;
    }
    if (event.keyword == QStringLiteral("音量小点")) {
        m_offlineArmed = false;
        m_commandWindow.stop();
        emit volumeDeltaRequested(-10);
        return;
    }
    if (event.keyword == QStringLiteral("小龙小龙")) {
        if (mediaBusy())
            return;
        if (m_capability && m_capability->onlineAiAvailable()) {
            m_offlineArmed = false;
            m_commandWindow.stop();
            emit userMessage(QStringLiteral("我在听，请说吧"));
            prepare(PendingAction::StartVoice);
        } else {
            m_offlineArmed = true;
            m_commandWindow.start();
            const QString reason = m_capability
                ? m_capability->unavailableReason() : QStringLiteral("联网语音不可用");
            emit userMessage(QStringLiteral("%1，已进入离线语音模式").arg(reason));
        }
        return;
    }
    if (event.keyword == QStringLiteral("陪我说话")) {
        if (!m_offlineArmed)
            return;
        m_offlineArmed = false;
        m_commandWindow.stop();
        prepare(PendingAction::PlayCompanion);
        return;
    }
    qInfo().noquote() << "KWS keyword has no LongPet policy:" << event.keyword;
}

void VoiceCommandDispatcher::prepare(PendingAction action)
{
    if (action == PendingAction::StartVoice && m_voice
        && m_voice->snapshot().isActive()) {
        action = PendingAction::RestartVoice;
    }
    m_pending = action;
    m_resumeTimer.stop();
    if (m_companion && m_companion->isActive()) {
        m_companion->stop();
        return;
    }
    if (!m_kws || !m_kws->isRunning() || m_kws->isPaused()) {
        performPending();
        return;
    }
    m_pauseTimeout.start();
    m_kws->pause();
}

void VoiceCommandDispatcher::performPending()
{
    if (m_pending == PendingAction::None)
        return;
    const PendingAction action = m_pending;
    m_pending = PendingAction::None;
    m_pauseTimeout.stop();
    if (action == PendingAction::PlayCompanion) {
        QString error;
        if (!m_companion || !m_companion->start(&error)) {
            emit userMessage(error.isEmpty()
                ? QStringLiteral("离线陪伴暂时不可用") : error);
            scheduleKwsResume();
        }
        return;
    }
    if (action == PendingAction::ContactFamily) {
        emit familyContactRequested();
        return;
    }
    if (!m_voice) {
        emit userMessage(QStringLiteral("语音交互服务不可用"));
        scheduleKwsResume();
        return;
    }
    const VoiceInteractionResult result = action == PendingAction::RestartVoice
        ? m_voice->restartInteraction() : m_voice->startInteraction();
    if (!result.success) {
        emit userMessage(result.error);
        scheduleKwsResume();
    }
}

void VoiceCommandDispatcher::scheduleKwsResume()
{
    if (!m_kws || !m_configuration.enabled || mediaBusy())
        return;
    m_resumeTimer.start();
}

bool VoiceCommandDispatcher::mediaBusy() const
{
    return (m_voice && m_voice->snapshot().isActive())
        || (m_companion && m_companion->isActive());
}
