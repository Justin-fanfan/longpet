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
    Error
};

struct KeywordDetection {
    QString keyword;
    QString signal;
    int code = 0;
    QString source;
    QDateTime timestamp;
};

struct KeywordSpottingStatus {
    KeywordSpottingRuntimeState state = KeywordSpottingRuntimeState::Disabled;
    bool available = false;
    bool listening = false;
    QString summary = QStringLiteral("关键词识别未启动");
    QString lastKeyword;
    QDateTime lastDetectedAt;
};

Q_DECLARE_METATYPE(KeywordDetection)
Q_DECLARE_METATYPE(KeywordSemantic)
Q_DECLARE_METATYPE(KeywordSpottingStatus)
