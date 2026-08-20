#pragma once

#include "model/KeywordSpottingModels.h"

#include <QHash>
#include <QObject>

#include <functional>

class KeywordSpottingAdapter;

class KeywordSpottingService final : public QObject {
    Q_OBJECT

public:
    using Clock = std::function<QDateTime()>;

    explicit KeywordSpottingService(KeywordSpottingAdapter* adapter,
                                    Clock clock = {},
                                    QObject* parent = nullptr);

    KeywordSpottingStatus status() const;
    static KeywordSemantic semanticFor(const KeywordDetection& detection);
    static int defaultCooldownMs();

public slots:
    void handleDetection(const KeywordDetection& detection);
    void handleRuntimeStatus(const KeywordSpottingStatus& status);

signals:
    void statusChanged(const KeywordSpottingStatus& status);
    void keywordDetected(const KeywordDetection& detection);
    void semanticDetected(KeywordSemantic semantic, const QString& keyword);

private:
    KeywordSpottingAdapter* m_adapter = nullptr;
    Clock m_clock;
    KeywordSpottingStatus m_status;
    QHash<int, QDateTime> m_lastSemanticAt;
};
