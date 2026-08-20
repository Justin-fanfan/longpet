#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>

enum class KeywordSemantic {
    Unknown,
    Greeting,
    Emergency,
    Stop,
    Acknowledge,
    Complete
};

enum class KeywordSpottingRuntimeState {
    Disabled,
    Starting,
    Listening,
    Degraded,
    Error
};

struct KeywordDetection {
    QString keyword;
    QString signal;
    int code = 0;
    QString source;
    QDateTime timestamp;
};

struct KeywordSpottingConfig {
    bool enabled = false;
    QString audioDevice = QStringLiteral("hw:0,0");
    int captureSampleRate = 44'100;
    int captureChannels = 2;
    int microphoneChannel = 0;
    double threshold = 0.25;
    double score = 1.5;
};

struct KeywordSpottingStatus {
    KeywordSpottingRuntimeState state = KeywordSpottingRuntimeState::Disabled;
    bool enabled = false;
    bool available = false;
    bool running = false;
    bool listening = false;
    QString summary = QStringLiteral("关键词识别未启动");
    QString lastKeyword;
    QDateTime lastDetectedAt;
    qint64 workerPid = 0;
    QDateTime startedAt;
    QString errorDetail;
    QString audioDevice;
    int captureSampleRate = 44'100;
    int captureChannels = 2;
    int microphoneChannel = 0;
    double threshold = 0.25;
    double score = 1.5;
    double inputRms = 0.0;
    double inputPeak = 0.0;
    int droppedUtterances = 0;
    double lastDecodeElapsedMs = 0.0;
    double lastRtf = 0.0;
    double lastKeywordLatencyMs = 0.0;
};

Q_DECLARE_METATYPE(KeywordDetection)
Q_DECLARE_METATYPE(KeywordSpottingConfig)
Q_DECLARE_METATYPE(KeywordSemantic)
Q_DECLARE_METATYPE(KeywordSpottingStatus)
