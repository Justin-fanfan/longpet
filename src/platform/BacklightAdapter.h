#pragma once

#include <QObject>
#include <QString>

class BacklightAdapter final : public QObject {
    Q_OBJECT

public:
    explicit BacklightAdapter(QObject* parent = nullptr);
    explicit BacklightAdapter(const QString& sysfsRoot, QObject* parent = nullptr);

    bool start();
    void stop();
    bool isAvailable() const;
    int brightnessLevels() const;
    QString devicePath() const;

    static int percentToRaw(int percent, int maximum);
    static int rawToPercent(int raw, int maximum);

public slots:
    void applyBrightness(int percent);

signals:
    void controlStateChanged(bool available, int levels, const QString& summary);
    void brightnessApplied(int effectivePercent);
    void errorOccurred(const QString& error);

private:
    bool readInteger(const QString& fileName, int* value, QString* error = nullptr) const;
    void publishUnavailable(const QString& detail);

    QString m_sysfsRoot;
    QString m_devicePath;
    int m_maximum = 0;
    bool m_available = false;
    QString m_summary;
};
