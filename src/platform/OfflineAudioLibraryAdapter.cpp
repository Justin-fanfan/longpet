#include "OfflineAudioLibraryAdapter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <utility>

OfflineAudioLibraryAdapter::OfflineAudioLibraryAdapter(QString directory)
    : m_directory(std::move(directory))
{
}

QStringList OfflineAudioLibraryAdapter::clipIds(QString* error) const
{
    if (error)
        error->clear();
    const QDir directory(m_directory);
    if (!directory.exists()) {
        if (error)
            *error = QStringLiteral("离线陪伴音频目录不存在：%1").arg(m_directory);
        return {};
    }
    return directory.entryList(
        {QStringLiteral("*.wav"), QStringLiteral("*.mp3"),
         QStringLiteral("*.ogg"), QStringLiteral("*.flac")},
        QDir::Files | QDir::Readable, QDir::Name);
}

QByteArray OfflineAudioLibraryAdapter::loadClip(const QString& clipId,
                                                QString* error) const
{
    if (error)
        error->clear();
    const QString fileName = QFileInfo(clipId).fileName();
    if (fileName != clipId || fileName.isEmpty()) {
        if (error)
            *error = QStringLiteral("离线音频名称无效");
        return {};
    }
    const QString path = QDir(m_directory).filePath(fileName);
    constexpr qint64 MaximumOfflineClipBytes = 16 * 1024 * 1024;
    if (QFileInfo(path).size() > MaximumOfflineClipBytes) {
        if (error)
            *error = QStringLiteral("离线音频 %1 超过 16 MiB 限制").arg(fileName);
        return {};
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取离线音频 %1：%2")
                .arg(fileName, file.errorString());
        return {};
    }
    const QByteArray audio = file.readAll();
    if (audio.isEmpty() && error)
        *error = QStringLiteral("离线音频 %1 为空").arg(fileName);
    return audio;
}
