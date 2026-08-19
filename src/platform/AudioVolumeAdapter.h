#pragma once

#include <QObject>
#include <QString>

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

    void* m_mixerHandle = nullptr;
    bool m_available = false;
    QString m_summary;
};
