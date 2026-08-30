#pragma once

#include <QtGlobal>

struct VoiceActivityUpdate {
    bool speechDetected = false;
    bool shouldStop = false;
};

class VoiceActivityDetector final {
public:
    VoiceActivityDetector(double thresholdDb = -42.0,
                          int silenceTimeoutMs = 900,
                          int minimumRecordingMs = 600,
                          int minimumSpeechMs = 160);

    void reset();
    VoiceActivityUpdate process(qint64 capturedMs, double levelDb);
    bool hasDetectedSpeech() const;

private:
    double m_thresholdDb = -42.0;
    int m_silenceTimeoutMs = 900;
    int m_minimumRecordingMs = 600;
    int m_minimumSpeechMs = 160;
    qint64 m_lastCapturedMs = 0;
    qint64 m_lastVoiceMs = -1;
    int m_consecutiveSpeechMs = 0;
    bool m_speechDetected = false;
};
