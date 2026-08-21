#include "DiagnosticsService.h"

DiagnosticsService::DiagnosticsService(int capacity, QObject* parent)
    : QObject(parent), m_capacity(qBound(1, capacity, 1'000))
{
    m_events.reserve(m_capacity);
}

int DiagnosticsService::capacity() const { return m_capacity; }
int DiagnosticsService::size() const { return m_events.size(); }

QList<DiagnosticEvent> DiagnosticsService::events() const
{
    return QList<DiagnosticEvent>(m_events.cbegin(), m_events.cend());
}

void DiagnosticsService::record(DiagnosticSource source, DiagnosticLevel level,
                                const QString& event, const QString& detail)
{
    DiagnosticEvent item {QDateTime::currentDateTime(), source, level,
                          event.simplified(), detail.simplified()};
    if (m_events.size() == m_capacity)
        m_events.removeFirst();
    m_events.append(item);
    emit eventAdded(item);
}

void DiagnosticsService::clear()
{
    if (m_events.isEmpty())
        return;
    m_events.clear();
    emit eventsCleared();
}
