#pragma once

#include "services/OfflineVoicePorts.h"

class OfflineAudioLibraryAdapter final : public OfflineAudioLibraryPort {
public:
    explicit OfflineAudioLibraryAdapter(QString directory);

    QStringList clipIds(QString* error = nullptr) const override;
    QByteArray loadClip(const QString& clipId,
                        QString* error = nullptr) const override;

private:
    QString m_directory;
};
