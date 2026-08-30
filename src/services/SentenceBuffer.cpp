#include "SentenceBuffer.h"

#include <QtGlobal>

namespace {
bool isSentenceBoundary(QChar character)
{
    return character == QLatin1Char('.')
        || character == QLatin1Char('!')
        || character == QLatin1Char('?')
        || character == QLatin1Char(';')
        || character == QLatin1Char('\n')
        || character == QChar(0x3002)  // 。
        || character == QChar(0xff01)  // ！
        || character == QChar(0xff1f)  // ？
        || character == QChar(0xff1b)  // ；
        || character == QChar(0x2026); // …
}

bool isTrailingCloser(QChar character)
{
    return character.isSpace()
        || character == QChar(0x2019)  // ’
        || character == QChar(0x201d)  // ”
        || character == QChar(0x3009)  // 〉
        || character == QChar(0x300b)  // 》
        || character == QChar(0x300d)  // 」
        || character == QChar(0x300f)  // 』
        || character == QChar(0x3011)  // 】
        || character == QChar(0xff09); // ）
}

bool isSoftBoundary(QChar character)
{
    return character.isSpace()
        || character == QLatin1Char(',')
        || character == QChar(0xff0c)  // ，
        || character == QChar(0x3001)  // 、
        || character == QChar(0xff1a); // ：
}
}

SentenceBuffer::SentenceBuffer(int minimumCharacters, int maximumCharacters)
    : m_minimumCharacters(qMax(1, minimumCharacters)),
      m_maximumCharacters(qMax(m_minimumCharacters, maximumCharacters))
{
}

QStringList SentenceBuffer::append(const QString& delta)
{
    m_buffer.append(delta);
    return takeReady(false);
}

QStringList SentenceBuffer::flush()
{
    return takeReady(true);
}

void SentenceBuffer::clear()
{
    m_buffer.clear();
}

QString SentenceBuffer::pendingText() const
{
    return m_buffer;
}

QStringList SentenceBuffer::takeReady(bool flushing)
{
    QStringList sentences;
    while (!m_buffer.isEmpty()) {
        int cut = -1;
        for (int i = 0; i < m_buffer.size(); ++i) {
            if (!isSentenceBoundary(m_buffer.at(i)))
                continue;

            int candidateEnd = i + 1;
            while (candidateEnd < m_buffer.size()
                   && isTrailingCloser(m_buffer.at(candidateEnd))) {
                ++candidateEnd;
            }
            // A closing quote may arrive in the next SSE delta. Holding a boundary
            // at the current end avoids sending the quote as a separate TTS item.
            if (!flushing && candidateEnd == m_buffer.size())
                break;
            if (m_buffer.left(candidateEnd).trimmed().size()
                >= m_minimumCharacters) {
                cut = candidateEnd;
                break;
            }
        }

        if (cut < 0 && m_buffer.size() >= m_maximumCharacters) {
            cut = m_maximumCharacters;
            for (int i = m_maximumCharacters - 1;
                 i >= m_minimumCharacters; --i) {
                if (isSoftBoundary(m_buffer.at(i))) {
                    cut = i + 1;
                    break;
                }
            }
        }
        if (cut < 0 && flushing)
            cut = m_buffer.size();
        if (cut < 0)
            break;

        const QString sentence = m_buffer.left(cut).trimmed();
        m_buffer.remove(0, cut);
        if (!sentence.isEmpty())
            sentences.append(sentence);
    }
    return sentences;
}
