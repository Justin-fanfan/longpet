#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

class OfflineAudioLibraryPort {
public:
    virtual ~OfflineAudioLibraryPort() = default;

    virtual QStringList clipIds(QString* error = nullptr) const = 0;
    virtual QByteArray loadClip(const QString& clipId,
                                QString* error = nullptr) const = 0;
};
