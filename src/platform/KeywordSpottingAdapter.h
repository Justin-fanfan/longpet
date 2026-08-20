#pragma once

#include "model/KeywordSpottingModels.h"

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QTimer>

class KeywordSpottingAdapter final : public QObject {
    Q_OBJECT

public:
    struct Options {
        bool enabled = false;
        QString runtimeRoot;
        QString pythonExecutable = QStringLiteral("python3");
        double threshold = 0.25;
        double score = 1.5;
        int startupTimeoutMs = 90'000;
    };

    explicit KeywordSpottingAdapter(QObject* parent = nullptr);
    explicit KeywordSpottingAdapter(const Options& options,
                                    QObject* parent = nullptr);
    ~KeywordSpottingAdapter() override;

    bool start();
    void stop();
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

private:
    void readStandardOutput();
    void readStandardError();
    void handleFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void publishStatus(KeywordSpottingRuntimeState state, bool available,
                       bool listening, const QString& summary);
    bool validateRuntime(QString* error) const;

    Options m_options;
    QProcess m_process;
    QTimer m_startupTimer;
    QByteArray m_stdoutBuffer;
    QByteArray m_stderrBuffer;
    KeywordSpottingStatus m_status;
    bool m_stopping = false;
};
