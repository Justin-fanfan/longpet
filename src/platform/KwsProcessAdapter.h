#pragma once

#include "model/AiModels.h"
#include "services/KwsPorts.h"

#include <QProcess>
#include <QTimer>

class KwsProcessAdapter final : public KwsPort {
    Q_OBJECT

public:
    explicit KwsProcessAdapter(const KwsConfiguration& configuration,
                               QObject* parent = nullptr);
    ~KwsProcessAdapter() override;

    void start() override;
    void pause() override;
    void resume() override;
    void stop() override;
    bool isRunning() const override;
    bool isPaused() const override;

private:
    void startProcess();
    void sendCommand(const QString& command);
    void consumeOutput();
    void processLine(const QByteArray& line);
    void scheduleRestart(const QString& diagnostic);

    KwsConfiguration m_configuration;
    QProcess m_process;
    QTimer m_restartTimer;
    QTimer m_stopTimer;
    QByteArray m_stdoutBuffer;
    bool m_started = false;
    bool m_ready = false;
    bool m_paused = false;
    bool m_pauseRequested = false;
    bool m_stopping = false;
    bool m_failureReported = false;
};
