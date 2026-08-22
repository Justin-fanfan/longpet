#pragma once

#include "model/DiagnosticsModels.h"

#include <QString>

class CameraDeviceAdapter final {
public:
    explicit CameraDeviceAdapter(QString deviceRoot = QStringLiteral("/dev"));
    QList<CameraDevice> cameras() const;
    bool isCameraAvailable(int index) const;

private:
    QString m_deviceRoot;
};
