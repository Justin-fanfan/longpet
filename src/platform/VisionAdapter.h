#pragma once

#include "model/VisionModels.h"

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <QVector>

class VisionAdapter final : public QObject {
    Q_OBJECT

public:
    struct Options {
        bool enabled = false;
        QString runtimeRoot;
        QString workerScript = QStringLiteral("src/vision_worker.py");
        QString pythonExecutable = QStringLiteral("python3");
        int cameraIndex = 0;
        int frameWidth = 320;
        int frameHeight = 240;
        int targetFps = 5;
        // Foreground-only fall detection has not met the safety acceptance
        // threshold. Keep it opt-in and expose candidates only.
        bool fallEnabled = false;
        bool waveEnabled = true;
        int startupTimeoutMs = 30'000;
        int niceAdjustment = 5;
        int killFallbackMs = 2'500;
        QVector<int> retryDelaysMs {5'000, 30'000, 120'000};
        QStringList additionalArguments;
    };

    explicit VisionAdapter(QObject* parent = nullptr);
    explicit VisionAdapter(const Options& options, QObject* parent = nullptr);
    ~VisionAdapter() override;

    bool start();
    void stop();
    bool restart();
    bool setEnabled(bool enabled);
    bool reconfigure(const Options& options, QString* error = nullptr);
    bool isRunning() const;
    VisionStatus status() const;
    Options options() const;

    static Options defaultOptions();
    static bool parseDetectionEvent(const QByteArray& line,
                                    VisionDetection* detection);
    static bool parseRuntimeStatusEvent(const QByteArray& line,
                                        VisionStatus* status);

signals:
    void detectionReceived(const VisionDetection& detection);
    void statusChanged(const VisionStatus& status);
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
    void publishStatus(VisionRuntimeState state, bool available,
                       bool cameraAvailable, bool monitoring,
                       const QString& summary, double effectiveFps = 0.0,
                       double frameTimeMs = 0.0, int cameraIndex = -1);
    bool validateRuntime(QString* error) const;

    Options m_options;
    QProcess m_process;
    QTimer m_startupTimer;
    QTimer m_killTimer;
    QTimer m_retryTimer;
    QTimer m_stabilityTimer;
    QByteArray m_stdoutBuffer;
    QByteArray m_stderrBuffer;
    VisionStatus m_status;
    bool m_stopping = false;
    bool m_restartAfterStop = false;
    bool m_nonRecoverableFailure = false;
    int m_retryAttempt = 0;
    qint64 m_processGroupId = 0;
};
