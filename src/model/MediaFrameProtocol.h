#pragma once

#include <QByteArray>
#include <QString>

enum class MediaStreamType : quint8 {
    DeviceVideo = 1,
    FamilyVideo = 2,
    DeviceAudio = 3,
    FamilyAudio = 4,
    Control = 5
};

struct MediaFrame {
    quint8 version = 0;
    MediaStreamType streamType = MediaStreamType::Control;
    quint16 flags = 0;
    quint32 sequence = 0;
    quint64 timestampUsec = 0;
    QByteArray payload;
};

namespace MediaFrameProtocol {
constexpr quint8 Version = 1;
constexpr qsizetype HeaderSize = 24;
constexpr qsizetype MaximumPayloadSize = 2 * 1024 * 1024;

QByteArray encode(MediaStreamType streamType,
                  quint32 sequence,
                  quint64 timestampUsec,
                  const QByteArray& payload,
                  quint16 flags = 0);
bool decode(const QByteArray& bytes, MediaFrame* frame, QString* error = nullptr);
}
