#pragma once

#include "model/DiagnosticsModels.h"

#include <functional>

class AudioDeviceAdapter final {
public:
    using Provider = std::function<QList<AudioInputDevice>()>;

    explicit AudioDeviceAdapter(Provider provider = {});
    QList<AudioInputDevice> inputDevices() const;
    bool isInputAvailable(const QString& id) const;

private:
    static QList<AudioInputDevice> enumerateSystemInputs();
    Provider m_provider;
};
