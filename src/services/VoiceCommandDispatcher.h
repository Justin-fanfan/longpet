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
    void notifyEmergencyActivity(bool active) { m_emergencyActive = active; }

signals:
    void emergencyRequested();
    void remindersRequested();
    void familyContactRequested();
    void homeRequested();
    void volumeDeltaRequested(int delta);
    void localTimeRequested(const QString& time);
    void companionActivityChanged(bool active);
    void userMessage(const QString& message);

private:
    enum class PendingAction {
        None,
        StartVoice,
        RestartVoice,
        PlayCompanion
    };

    void handleKeyword(const KwsEvent& event);
    void prepare(PendingAction action);
    void performPending();
    void clearCommandWindow();
    bool mediaBusy() const;

    KwsConfiguration m_configuration;
    KwsPort* m_kws = nullptr;
    VoiceCapabilityService* m_capability = nullptr;
    VoiceInteractionService* m_voice = nullptr;
    LocalCompanionService* m_companion = nullptr;
    QTimer m_commandWindow;
    PendingAction m_pending = PendingAction::None;
    bool m_started = false;
    bool m_externalMediaActive = false;
    bool m_emergencyActive = false;
    bool m_offlineArmed = false;
};
