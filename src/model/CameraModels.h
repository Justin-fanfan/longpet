#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QMetaType>

struct CameraFrame {
    QByteArray jpeg;
    quint64 sequence = 0;
    QDateTime timestamp;
    int rotationDegrees = 0;

    bool isValid() const
    {
        return !jpeg.isEmpty() && sequence > 0 && timestamp.isValid();
    }
};

Q_DECLARE_METATYPE(CameraFrame)
