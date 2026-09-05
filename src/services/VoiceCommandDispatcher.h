#pragma once

#include "model/AiModels.h"

#include <QObject>
#include <QTimer>

class KwsPort;
class LocalCompanionService;
class VoiceCapabilityService;
class VoiceInteractionService;

class VoiceCommandDispatcher final : public QObject {
    Q_OBJECT

public:
    VoiceCommandDispatcher(const KwsConfiguration& configuration,
                           KwsPort* kws,
                           VoiceCapabilityService* capability,
                           VoiceInteractionService* voice,
                           LocalCompanionService* companion,
                           QObject* parent = nullptr);

    void start();
    void stop();
    void requestStartInteraction();
    void requestRestartInteraction();
    void requestCancelInteraction();
    void notifyExternalMediaActivity(bool active);

signals:
    void emergencyRequested();
    void remindersRequested();
    void familyContactRequested();
    void homeRequested();
    void volumeDeltaRequested(int delta);
    void userMessage(const QString& message);

private:
    enum class PendingAction {
        None,
        StartVoice,
        RestartVoice,
        PlayCompanion,
        ContactFamily
    };

    void handleKeyword(const KwsEvent& event);
    void prepare(PendingAction action);
    void performPending();
    void scheduleKwsResume();
    bool mediaBusy() const;

    KwsConfiguration m_configuration;
    KwsPort* m_kws = nullptr;
    VoiceCapabilityService* m_capability = nullptr;
    VoiceInteractionService* m_voice = nullptr;
    LocalCompanionService* m_companion = nullptr;
    QTimer m_pauseTimeout;
    QTimer m_resumeTimer;
    QTimer m_commandWindow;
    PendingAction m_pending = PendingAction::None;
    qint64 m_ignoreKeywordsUntilMs = 0;
    bool m_offlineArmed = false;
};
