#pragma once

#include <QList>
#include <QMetaType>
#include <QString>

struct WifiNetwork {
    QString ssid;
    int signalStrength = 0;
    QString security;
    bool connected = false;
    bool requiresPassword = false;
    bool supported = true;
};

Q_DECLARE_METATYPE(WifiNetwork)
Q_DECLARE_METATYPE(QList<WifiNetwork>)
