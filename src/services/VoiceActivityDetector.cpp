#include "VoiceActivityDetector.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <vector>

VoiceActivityDetector::VoiceActivityDetector(double thresholdDb,
                                             int silenceTimeoutMs,
                                             int minimumRecordingMs,
                                             int minimumSpeechMs,
                                             double noiseRatio)
    : m_absoluteThresholdDb(thresholdDb),
      m_noiseMarginDb(20.0 * std::log10(qMax(1.0, noiseRatio))),
      m_silenceTimeoutMs(qMax(1, silenceTimeoutMs)),
      m_minimumRecordingMs(qMax(0, minimumRecordingMs)),
      m_minimumSpeechMs(qMax(1, minimumSpeechMs))
{
}

void VoiceActivityDetector::reset()
{
    m_lastCapturedMs = 0;
    m_lastVoiceMs = -1;
    m_noiseFloorDb = -96.0;
    m_effectiveThresholdDb = m_absoluteThresholdDb;
    m_levelHistory.clear();
    m_speechDetected = false;
}

VoiceActivityUpdate VoiceActivityDetector::process(qint64 capturedMs,
                                                   double levelDb)
{
    VoiceActivityUpdate update;
    capturedMs = qMax(m_lastCapturedMs, capturedMs);
    levelDb = qBound(-96.0, levelDb, 0.0);
    m_lastCapturedMs = capturedMs;

    if (!m_speechDetected) {
        m_levelHistory.push_back({capturedMs, levelDb});
        const qint64 historyMs = qMax<qint64>(
            2'000, m_silenceTimeoutMs + m_minimumSpeechMs + 500);
        while (!m_levelHistory.empty()
               && capturedMs - m_levelHistory.front().capturedMs > historyMs) {
            m_levelHistory.pop_front();
        }
        m_noiseFloorDb = updateNoiseFloor();
        m_effectiveThresholdDb = qMax(
            m_absoluteThresholdDb, m_noiseFloorDb + m_noiseMarginDb);
        qint64 historyLastVoiceMs = -1;
        if (detectSpeechInHistory(m_effectiveThresholdDb,
                                  &historyLastVoiceMs)) {
            m_speechDetected = true;
            update.speechDetected = true;
            m_lastVoiceMs = historyLastVoiceMs;
        }
    } else if (levelDb >= m_effectiveThresholdDb) {
        m_lastVoiceMs = capturedMs;
    }

    update.noiseFloorDb = m_noiseFloorDb;
    update.effectiveThresholdDb = m_effectiveThresholdDb;
    update.shouldStop = m_speechDetected
        && capturedMs >= m_minimumRecordingMs
        && m_lastVoiceMs >= 0
        && capturedMs - m_lastVoiceMs >= m_silenceTimeoutMs;
    return update;
}

double VoiceActivityDetector::updateNoiseFloor()
{
    if (m_levelHistory.empty())
        return -96.0;
    std::vector<double> levels;
    levels.reserve(m_levelHistory.size());
    for (const LevelFrame& frame : m_levelHistory)
        levels.push_back(frame.levelDb);
    // A low percentile follows stationary USB microphone noise without letting
    // normal speech peaks immediately raise the floor.
    const std::size_t index = (levels.size() - 1) / 5;
    std::nth_element(levels.begin(), levels.begin() + index, levels.end());
    return levels[index];
}

bool VoiceActivityDetector::detectSpeechInHistory(
    double thresholdDb, qint64* lastVoiceMs) const
{
    int consecutiveMs = 0;
    qint64 previousMs = 0;
    qint64 latestVoiceMs = -1;
    bool detected = false;
    for (const LevelFrame& frame : m_levelHistory) {
        const int elapsed = previousMs == 0 ? 0 : static_cast<int>(
            qBound<qint64>(qint64(0), frame.capturedMs - previousMs,
                           qint64(200)));
        previousMs = frame.capturedMs;
        if (frame.levelDb >= thresholdDb) {
            consecutiveMs += elapsed;
            latestVoiceMs = frame.capturedMs;
            detected = detected || consecutiveMs >= m_minimumSpeechMs;
        } else {
            consecutiveMs = 0;
        }
    }
    if (detected && lastVoiceMs)
        *lastVoiceMs = latestVoiceMs;
    return detected;
}

bool VoiceActivityDetector::hasDetectedSpeech() const
{
    return m_speechDetected;
}
