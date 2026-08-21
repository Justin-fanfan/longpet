#include "CameraDeviceAdapter.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#include <utility>

CameraDeviceAdapter::CameraDeviceAdapter(QString deviceRoot)
    : m_deviceRoot(std::move(deviceRoot))
{
}

QList<CameraDevice> CameraDeviceAdapter::cameras() const
{
    QList<CameraDevice> result;
    const QDir root(m_deviceRoot);
    const QRegularExpression pattern(QStringLiteral("^video(\\d+)$"));
    const QStringList entries = root.entryList(
        {QStringLiteral("video*")}, QDir::System | QDir::Files, QDir::Name);
    for (const QString& entry : entries) {
        const auto match = pattern.match(entry);
        if (!match.hasMatch())
            continue;
        const int index = match.captured(1).toInt();
        const QString id = root.filePath(entry);
        result.append({id, index, QStringLiteral("Camera %1 (%2)").arg(index).arg(id),
                       QFileInfo(id).exists()});
    }
    return result;
}

bool CameraDeviceAdapter::isCameraAvailable(int index) const
{
    for (const CameraDevice& camera : cameras()) {
        if (camera.index == index && camera.available)
            return true;
    }
    return false;
}
