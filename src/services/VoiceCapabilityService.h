#pragma once

#include "model/AiModels.h"

#include <QObject>
#include <QTimer>

class SystemService;

class VoiceCapabilityService final : public QObject {
    Q_OBJECT

public:
    VoiceCapabilityService(const AiConfiguration& configuration,
                           SystemService* systemService,
                           QObject* parent = nullptr);

    bool onlineAiAvailable() const;
    QString unavailableReason() const;
    void reportProviderAvailability(bool available, const QString& reason);

signals:
    void availabilityChanged(bool available, const QString& reason);

private:
    void reevaluate();

    AiConfiguration m_configuration;
    SystemService* m_systemService = nullptr;
    QTimer m_retryTimer;
    bool m_providerDegraded = false;
    bool m_available = false;
    QString m_reason;
    QString m_providerReason;
};
