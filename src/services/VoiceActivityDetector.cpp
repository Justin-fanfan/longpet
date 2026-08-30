#include "VoiceActivityDetector.h"

#include <QtGlobal>

VoiceActivityDetector::VoiceActivityDetector(double thresholdDb,
                                             int silenceTimeoutMs,
                                             int minimumRecordingMs,
                                             int minimumSpeechMs)
    : m_thresholdDb(thresholdDb),
      m_silenceTimeoutMs(qMax(1, silenceTimeoutMs)),
      m_minimumRecordingMs(qMax(0, minimumRecordingMs)),
      m_minimumSpeechMs(qMax(1, minimumSpeechMs))
{
}

void VoiceActivityDetector::reset()
{
    m_lastCapturedMs = 0;
    m_lastVoiceMs = -1;
    m_consecutiveSpeechMs = 0;
    m_speechDetected = false;
}

VoiceActivityUpdate VoiceActivityDetector::process(qint64 capturedMs,
                                                   double levelDb)
{
    VoiceActivityUpdate update;
    capturedMs = qMax(m_lastCapturedMs, capturedMs);
    const int elapsed = static_cast<int>(qMin<qint64>(
        capturedMs - m_lastCapturedMs, 2'000));
    m_lastCapturedMs = capturedMs;

    if (levelDb >= m_thresholdDb) {
        m_consecutiveSpeechMs += elapsed;
        if (!m_speechDetected && m_consecutiveSpeechMs >= m_minimumSpeechMs) {
            m_speechDetected = true;
            update.speechDetected = true;
        }
        if (m_speechDetected)
            m_lastVoiceMs = capturedMs;
    } else if (!m_speechDetected) {
        m_consecutiveSpeechMs = 0;
    }

    update.shouldStop = m_speechDetected
        && capturedMs >= m_minimumRecordingMs
        && m_lastVoiceMs >= 0
        && capturedMs - m_lastVoiceMs >= m_silenceTimeoutMs;
    return update;
}

bool VoiceActivityDetector::hasDetectedSpeech() const
{
    return m_speechDetected;
}
