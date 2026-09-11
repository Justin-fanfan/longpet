#pragma once

#include "model/MotionModels.h"

#include <QByteArray>
#include <QString>

namespace EspMotionProtocol {
QByteArray stopCommand();
QByteArray modeCommand(MotionControlMode mode);
QByteArray moveCommand(ChassisMotion motion, int speed);
QByteArray headCommand(HeadMotion motion, int stepUs);
QByteArray statusCommand();
bool parseStatusLine(const QByteArray& line, MotionStatusSnapshot* status,
                     QString* error = nullptr);
}

