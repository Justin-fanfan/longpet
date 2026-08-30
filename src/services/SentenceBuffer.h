#pragma once

#include <QString>
#include <QStringList>

class SentenceBuffer final {
public:
    SentenceBuffer(int minimumCharacters = 6, int maximumCharacters = 120);

    QStringList append(const QString& delta);
    QStringList flush();
    void clear();
    QString pendingText() const;

private:
    QStringList takeReady(bool flushing);

    int m_minimumCharacters = 6;
    int m_maximumCharacters = 120;
    QString m_buffer;
};
