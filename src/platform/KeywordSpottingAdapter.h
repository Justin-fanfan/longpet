#pragma once

#include "model/KeywordSpottingModels.h"

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVector>

class KeywordSpottingAdapter final : public QObject {
    Q_OBJECT

public:
    struct Options {
        bool enabled = false;
        QString runtimeRoot;
        QString pythonExecutable = QStringLiteral("python3");
        double threshold = 0.25;
        double score = 1.5;
        QString audioDevice = QStringLiteral("hw:0,0");
        int captureSampleRate = 44'100;
        int captureChannels = 2;
        int microphoneChannel = 0;
        int startupTimeoutMs = 90'000;
        int killFallbackMs = 2'500;
        QVector<int> retryDelaysMs {5'000, 30'000, 120'000};
    };

    explicit KeywordSpottingAdapter(QObject* parent = nullptr);
    explicit KeywordSpottingAdapter(const Options& options,
                                    QObject* parent = nullptr);
    ~KeywordSpottingAdapter() override;

    bool start();
    void stop();
    bool restart();
    bool setEnabled(bool enabled);
    bool reconfigure(const Options& options, QString* error = nullptr);
    bool isRunning() const;
    KeywordSpottingStatus status() const;
    Options options() const;

    static Options defaultOptions();
    static bool parseKeywordEvent(const QByteArray& line,
                                  KeywordDetection* detection);
    static bool parseRuntimeStatusEvent(const QByteArray& line,
                                        KeywordSpottingStatus* status);

signals:
    void keywordDetected(const KeywordDetection& detection);
    void statusChanged(const KeywordSpottingStatus& status);
    void diagnosticMessage(const QString& message);
    void recoveryScheduled(int attempt, int delayMs);
    void forceKillInvoked();

private:
    void readStandardOutput();
    void readStandardError();
    void handleFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void requestStop(bool restartAfterStop);
    void forceKill();
    void scheduleRecovery(const QString& reason);
    void updateStatusConfiguration();
    void publishStatus(KeywordSpottingRuntimeState state, bool available,
                       bool listening, const QString& summary);
    bool validateRuntime(QString* error) const;

    Options m_options;
    QProcess m_process;
    QTimer m_startupTimer;
    QTimer m_killTimer;
    QTimer m_retryTimer;
    QTimer m_stabilityTimer;
    QByteArray m_stdoutBuffer;
    QByteArray m_stderrBuffer;
    KeywordSpottingStatus m_status;
    bool m_stopping = false;
    bool m_restartAfterStop = false;
    bool m_nonRecoverableFailure = false;
    int m_retryAttempt = 0;
    qint64 m_processGroupId = 0;
};
