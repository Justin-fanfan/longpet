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
    KeywordSpottingConfig config() const;
    bool setEnabled(bool enabled);
    bool start();
    void stop();
    bool restart();
    bool reconfigure(const KeywordSpottingConfig& config,
                     QString* error = nullptr);
    void injectDiagnosticSemantic(KeywordSemantic semantic,
                                  const QString& keyword = {});

public slots:
    void handleDetection(const KeywordDetection& detection);
    void handleRuntimeStatus(const KeywordSpottingStatus& status);

signals:
    void statusChanged(const KeywordSpottingStatus& status);
    void keywordDetected(const KeywordDetection& detection);
    void semanticDetected(KeywordSemantic semantic, const QString& keyword);
    void diagnosticInjectionRequested(KeywordSemantic semantic,
                                      const QString& keyword);
    void adapterDiagnostic(const QString& message);
    void recoveryScheduled(int attempt, int delayMs);

private:
    KeywordSpottingAdapter* m_adapter = nullptr;
    Clock m_clock;
    KeywordSpottingStatus m_status;
    QHash<int, QDateTime> m_lastSemanticAt;
};
