#include "SseEventParser.h"

QList<QByteArray> SseEventParser::append(const QByteArray& chunk)
{
    QList<QByteArray> events;
    m_buffer.append(chunk);
    qsizetype newline = -1;
    while ((newline = m_buffer.indexOf('\n')) >= 0) {
        QByteArray line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);
        if (line.endsWith('\r'))
            line.chop(1);
        processLine(std::move(line), &events);
    }
    return events;
}

QList<QByteArray> SseEventParser::finish()
{
    QList<QByteArray> events;
    if (!m_buffer.isEmpty()) {
        QByteArray line = std::move(m_buffer);
        m_buffer.clear();
        if (line.endsWith('\r'))
            line.chop(1);
        processLine(std::move(line), &events);
    }
    dispatch(&events);
    return events;
}

void SseEventParser::reset()
{
    m_buffer.clear();
    m_dataLines.clear();
}

void SseEventParser::processLine(QByteArray line,
                                 QList<QByteArray>* events)
{
    if (line.isEmpty()) {
        dispatch(events);
        return;
    }
    if (line.startsWith(':'))
        return;
    if (line == QByteArrayLiteral("data")) {
        m_dataLines.append(QByteArray());
        return;
    }
    if (!line.startsWith("data:"))
        return;
    line.remove(0, 5);
    if (line.startsWith(' '))
        line.remove(0, 1);
    m_dataLines.append(std::move(line));
}

void SseEventParser::dispatch(QList<QByteArray>* events)
{
    if (m_dataLines.isEmpty())
        return;
    events->append(m_dataLines.join('\n'));
    m_dataLines.clear();
}
