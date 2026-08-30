#pragma once

#include <QByteArray>
#include <QList>

class SseEventParser final {
public:
    QList<QByteArray> append(const QByteArray& chunk);
    QList<QByteArray> finish();
    void reset();

private:
    void processLine(QByteArray line, QList<QByteArray>* events);
    void dispatch(QList<QByteArray>* events);

    QByteArray m_buffer;
    QList<QByteArray> m_dataLines;
};
