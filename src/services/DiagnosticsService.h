#pragma once

#include "model/DiagnosticsModels.h"

#include <QObject>
#include <QVector>

class DiagnosticsService final : public QObject {
    Q_OBJECT

public:
    explicit DiagnosticsService(int capacity = 200, QObject* parent = nullptr);
    int capacity() const;
    int size() const;
    QList<DiagnosticEvent> events() const;
    void record(DiagnosticSource source, DiagnosticLevel level,
                const QString& event, const QString& detail = {});
    void clear();

signals:
    void eventAdded(const DiagnosticEvent& event);
    void eventsCleared();

private:
    int m_capacity = 200;
    QVector<DiagnosticEvent> m_events;
};
