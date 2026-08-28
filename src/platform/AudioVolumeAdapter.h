#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTimer>

class AudioVolumeAdapter final : public QObject {
    Q_OBJECT

public:
    explicit AudioVolumeAdapter(QObject* parent = nullptr);
    ~AudioVolumeAdapter() override;

    bool start();
    void stop();
    bool isAvailable() const;

    static long scalePercentToRange(int percent, long minimum, long maximum);

public slots:
    void applyVolume(int percent);

signals:
    void controlStateChanged(bool available, const QString& summary);
    void volumeApplied(int percent);
    void errorOccurred(const QString& error);

private:
    void publishUnavailable(const QString& detail);
    void scheduleRetry();

    QTimer m_retryTimer;
    void* m_mixerHandle = nullptr;
    bool m_available = false;
    bool m_hasRequestedVolume = false;
    int m_retryAttempts = 0;
    int m_requestedVolume = 50;
    QByteArray m_playbackControlName;
    QString m_summary;
    QString m_lastUnavailableDetail;
};
