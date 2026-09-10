#pragma once

#include <QtGlobal>

#include <deque>

struct VoiceActivityUpdate {
    bool speechDetected = false;
    bool shouldStop = false;
    double noiseFloorDb = -96.0;
    double effectiveThresholdDb = -55.0;
};

class VoiceActivityDetector final {
public:
    VoiceActivityDetector(double thresholdDb = -55.0,
                          int silenceTimeoutMs = 900,
                          int minimumRecordingMs = 600,
                          int minimumSpeechMs = 160,
                          double noiseRatio = 2.0);

    void reset();
    VoiceActivityUpdate process(qint64 capturedMs, double levelDb);
    bool hasDetectedSpeech() const;

private:
    struct LevelFrame {
        qint64 capturedMs = 0;
        double levelDb = -96.0;
    };

    double updateNoiseFloor();
    bool detectSpeechInHistory(double thresholdDb, qint64* lastVoiceMs) const;

    double m_absoluteThresholdDb = -55.0;
    double m_noiseMarginDb = 6.0206;
    double m_noiseFloorDb = -96.0;
    double m_effectiveThresholdDb = -55.0;
    int m_silenceTimeoutMs = 900;
    int m_minimumRecordingMs = 600;
    int m_minimumSpeechMs = 160;
    qint64 m_lastCapturedMs = 0;
    qint64 m_lastVoiceMs = -1;
    std::deque<LevelFrame> m_levelHistory;
    bool m_speechDetected = false;
};
