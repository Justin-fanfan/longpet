#include "AudioDeviceAdapter.h"

#include <QSet>

#include <cstdlib>
#include <utility>

#ifdef LONGPET_HAS_ALSA
#include <alsa/asoundlib.h>
#endif

AudioDeviceAdapter::AudioDeviceAdapter(Provider provider)
    : m_provider(provider ? std::move(provider) : enumerateSystemInputs)
{
}

QList<AudioInputDevice> AudioDeviceAdapter::inputDevices() const
{
    return m_provider ? m_provider() : QList<AudioInputDevice>();
}

bool AudioDeviceAdapter::isInputAvailable(const QString& id) const
{
    const QString requested = id.trimmed();
    for (const AudioInputDevice& device : inputDevices()) {
        if (device.available && device.id == requested)
            return true;
    }
    return false;
}

QList<AudioInputDevice> AudioDeviceAdapter::enumerateSystemInputs()
{
    QList<AudioInputDevice> devices;
#ifdef LONGPET_HAS_ALSA
    void** hints = nullptr;
    if (snd_device_name_hint(-1, "pcm", &hints) < 0 || !hints)
        return devices;
    QSet<QString> seen;
    for (void** cursor = hints; *cursor; ++cursor) {
        char* name = snd_device_name_get_hint(*cursor, "NAME");
        char* description = snd_device_name_get_hint(*cursor, "DESC");
        char* io = snd_device_name_get_hint(*cursor, "IOID");
        const QString id = name ? QString::fromLocal8Bit(name).trimmed() : QString();
        const QString ioId = io ? QString::fromLatin1(io).trimmed() : QString();
        if (!id.isEmpty() && ioId != QStringLiteral("Output") && !seen.contains(id)) {
            QString display = description
                ? QString::fromLocal8Bit(description).simplified() : id;
            devices.append({id, display.isEmpty() ? id : display, true});
            seen.insert(id);
        }
        free(name);
        free(description);
        free(io);
    }
    snd_device_name_free_hint(hints);
#endif
    return devices;
}
