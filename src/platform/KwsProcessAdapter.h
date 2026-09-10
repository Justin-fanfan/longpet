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
    void killProcessTree();

    KwsConfiguration m_configuration;
    QProcess m_process;
    QTimer m_restartTimer;
    QTimer m_stopTimer;
    QTimer m_startupTimer;
    qint64 m_processGroup = 0;
    qint64 m_commandSequence = 0;
    qint64 m_waitingCommandId = 0;
    QString m_waitingCommand;
    QByteArray m_stdoutBuffer;
    bool m_started = false;
    bool m_ready = false;
    bool m_paused = false;
    bool m_pauseRequested = true;
    bool m_stopping = false;
    bool m_failureReported = false;
};
