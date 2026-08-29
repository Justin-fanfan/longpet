#include "MediaFrameProtocol.h"

#include <QtEndian>

#include <cstring>

namespace {
constexpr char Magic[4] = {'L', 'P', 'M', 'F'};

template<typename T>
void appendBigEndian(QByteArray* bytes, T value)
{
    const T encoded = qToBigEndian(value);
    bytes->append(reinterpret_cast<const char*>(&encoded), sizeof(T));
}

template<typename T>
T readBigEndian(const char* data)
{
    T encoded {};
    std::memcpy(&encoded, data, sizeof(T));
    return qFromBigEndian(encoded);
}
}

QByteArray MediaFrameProtocol::encode(MediaStreamType streamType,
                                      quint32 sequence,
                                      quint64 timestampUsec,
                                      const QByteArray& payload,
                                      quint16 flags)
{
    QByteArray bytes;
    bytes.reserve(HeaderSize + payload.size());
    bytes.append(Magic, 4);
    bytes.append(static_cast<char>(Version));
    bytes.append(static_cast<char>(streamType));
    appendBigEndian(&bytes, flags);
    appendBigEndian(&bytes, sequence);
    appendBigEndian(&bytes, timestampUsec);
    appendBigEndian(&bytes, static_cast<quint32>(payload.size()));
    bytes.append(payload);
    return bytes;
}

bool MediaFrameProtocol::decode(const QByteArray& bytes, MediaFrame* frame,
                                QString* error)
{
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    if (!frame)
        return fail(QStringLiteral("媒体帧输出对象为空"));
    if (bytes.size() < HeaderSize)
        return fail(QStringLiteral("媒体帧头不完整"));
    if (std::memcmp(bytes.constData(), Magic, 4) != 0)
        return fail(QStringLiteral("媒体帧 magic 无效"));

    const auto version = static_cast<quint8>(bytes.at(4));
    if (version != Version)
        return fail(QStringLiteral("不支持的媒体协议版本：%1").arg(version));
    const auto streamValue = static_cast<quint8>(bytes.at(5));
    if (streamValue < static_cast<quint8>(MediaStreamType::DeviceVideo)
        || streamValue > static_cast<quint8>(MediaStreamType::Control)) {
        return fail(QStringLiteral("媒体流类型无效"));
    }
    const quint32 payloadSize = readBigEndian<quint32>(bytes.constData() + 20);
    if (payloadSize > MaximumPayloadSize)
        return fail(QStringLiteral("媒体帧 payload 超过限制"));
    if (bytes.size() != HeaderSize + static_cast<qsizetype>(payloadSize))
        return fail(QStringLiteral("媒体帧 payload 长度不匹配"));

    frame->version = version;
    frame->streamType = static_cast<MediaStreamType>(streamValue);
    frame->flags = readBigEndian<quint16>(bytes.constData() + 6);
    frame->sequence = readBigEndian<quint32>(bytes.constData() + 8);
    frame->timestampUsec = readBigEndian<quint64>(bytes.constData() + 12);
    frame->payload = bytes.mid(HeaderSize, payloadSize);
    return true;
}
