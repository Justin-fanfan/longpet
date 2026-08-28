#include "AudioVolumeAdapter.h"

#include <QDebug>

#include <algorithm>

#ifdef LONGPET_HAS_ALSA
#include <alsa/asoundlib.h>
#endif

namespace {
constexpr int AudioRetryLimit = 30;

#ifdef LONGPET_HAS_ALSA
snd_mixer_elem_t* findElement(snd_mixer_t* mixer, const char* name)
{
    snd_mixer_selem_id_t* id = nullptr;
    snd_mixer_selem_id_alloca(&id);
    snd_mixer_selem_id_set_index(id, 0);
    snd_mixer_selem_id_set_name(id, name);
    return snd_mixer_find_selem(mixer, id);
}

QString alsaError(const QString& context, int code)
{
    return QStringLiteral("%1：%2").arg(context, QString::fromLocal8Bit(snd_strerror(code)));
}

QByteArray mixerCardId(const QByteArray& mixerDevice)
{
    constexpr char prefix[] = "hw:CARD=";
    if (!mixerDevice.startsWith(prefix))
        return {};
    QByteArray cardId = mixerDevice.mid(sizeof(prefix) - 1);
    const qsizetype optionSeparator = cardId.indexOf(',');
    if (optionSeparator >= 0)
        cardId.truncate(optionSeparator);
    return cardId;
}

bool setPlaybackVolume(snd_mixer_t* mixer, const char* name, int percent,
                       bool required, QString* error)
{
    snd_mixer_elem_t* element = findElement(mixer, name);
    if (!element) {
        if (required && error)
            *error = QStringLiteral("ALSA 缺少 %1 音量控件").arg(QString::fromLatin1(name));
        return !required;
    }

    long minimum = 0;
    long maximum = 0;
    if (snd_mixer_selem_get_playback_volume_range(element, &minimum, &maximum) < 0) {
        if (required && error)
            *error = QStringLiteral("无法读取 %1 音量范围").arg(QString::fromLatin1(name));
        return !required;
    }
    const long value = AudioVolumeAdapter::scalePercentToRange(
        percent, minimum, maximum);
    const int result = snd_mixer_selem_set_playback_volume_all(element, value);
    if (result < 0 && required && error)
        *error = alsaError(QStringLiteral("设置 %1 音量失败").arg(QString::fromLatin1(name)), result);
    return result >= 0 || !required;
}

bool setPlaybackSwitch(snd_mixer_t* mixer, const char* name, bool enabled)
{
    snd_mixer_elem_t* element = findElement(mixer, name);
    if (!element || !snd_mixer_selem_has_playback_switch(element))
        return true;
    return snd_mixer_selem_set_playback_switch_all(element, enabled ? 1 : 0) >= 0;
}

QByteArray playbackControlName(snd_mixer_t* mixer)
{
    for (const char* name : {"PCM", "Speaker"}) {
        if (findElement(mixer, name))
            return QByteArray(name);
    }
    return {};
}

QString cardSummary(const QByteArray& mixerDevice, const QByteArray& controlName)
{
    return QStringLiteral("%1 / %2 · ALSA 音量")
        .arg(QString::fromLocal8Bit(mixerDevice),
             QString::fromLatin1(controlName));
}
#endif
}

AudioVolumeAdapter::AudioVolumeAdapter(QObject* parent)
    : QObject(parent)
{
    m_retryTimer.setSingleShot(true);
    m_retryTimer.setInterval(1000);
    connect(&m_retryTimer, &QTimer::timeout, this, [this] {
        m_retryTimer.stop();
        if (!start())
            return;
        if (m_hasRequestedVolume)
            applyVolume(m_requestedVolume);
    });
}

AudioVolumeAdapter::~AudioVolumeAdapter()
{
    stop();
}

bool AudioVolumeAdapter::start()
{
    if (m_available)
        return true;

#ifdef LONGPET_HAS_ALSA
    const QByteArray mixerDevice = qEnvironmentVariable(
        "LONGPET_ALSA_MIXER_DEVICE", QStringLiteral("default")).toLocal8Bit();
    const QByteArray cardId = mixerCardId(mixerDevice);
    if (!cardId.isEmpty() && snd_card_get_index(cardId.constData()) < 0) {
        publishUnavailable(QStringLiteral("等待 ALSA 声卡：%1")
            .arg(QString::fromLocal8Bit(cardId)));
        scheduleRetry();
        return false;
    }

    snd_mixer_t* mixer = nullptr;
    int result = snd_mixer_open(&mixer, 0);
    if (result < 0) {
        publishUnavailable(alsaError(QStringLiteral("打开 ALSA mixer 失败"), result));
        scheduleRetry();
        return false;
    }

    result = snd_mixer_attach(mixer, mixerDevice.constData());
    if (result >= 0)
        result = snd_mixer_selem_register(mixer, nullptr, nullptr);
    if (result >= 0)
        result = snd_mixer_load(mixer);
    if (result < 0) {
        snd_mixer_close(mixer);
        publishUnavailable(alsaError(QStringLiteral("加载 ALSA mixer 失败"), result));
        scheduleRetry();
        return false;
    }
    const QByteArray controlName = playbackControlName(mixer);
    if (controlName.isEmpty()) {
        snd_mixer_close(mixer);
        publishUnavailable(QStringLiteral("ALSA mixer 中没有 PCM 或 Speaker 音量控件"));
        scheduleRetry();
        return false;
    }

    m_mixerHandle = mixer;
    m_available = true;
    m_playbackControlName = controlName;
    m_summary = cardSummary(mixerDevice, controlName);
    m_lastUnavailableDetail.clear();
    m_retryAttempts = 0;
    m_retryTimer.stop();
    qInfo() << "Audio volume adapter ready:" << m_summary;
    emit controlStateChanged(true, m_summary);
    return true;
#else
    publishUnavailable(QStringLiteral("当前构建未启用 ALSA"));
    return false;
#endif
}

void AudioVolumeAdapter::stop()
{
#ifdef LONGPET_HAS_ALSA
    if (m_mixerHandle)
        snd_mixer_close(static_cast<snd_mixer_t*>(m_mixerHandle));
#endif
    m_mixerHandle = nullptr;
    m_available = false;
    m_hasRequestedVolume = false;
    m_retryAttempts = 0;
    m_playbackControlName.clear();
    m_summary.clear();
    m_lastUnavailableDetail.clear();
    m_retryTimer.stop();
}

bool AudioVolumeAdapter::isAvailable() const
{
    return m_available;
}

long AudioVolumeAdapter::scalePercentToRange(int percent, long minimum, long maximum)
{
    if (maximum <= minimum)
        return minimum;
    const int bounded = std::clamp(percent, 0, 100);
    const long range = maximum - minimum;
    return minimum + (range * bounded + 50) / 100;
}

void AudioVolumeAdapter::applyVolume(int percent)
{
    const int bounded = std::clamp(percent, 0, 100);
    m_requestedVolume = bounded;
    m_hasRequestedVolume = true;
    if (!m_available) {
        if (m_retryTimer.isActive() || !start())
            return;
    }

#ifdef LONGPET_HAS_ALSA
    auto* mixer = static_cast<snd_mixer_t*>(m_mixerHandle);
    QString error;
    if (!setPlaybackVolume(mixer, m_playbackControlName.constData(), bounded, true, &error)) {
        qWarning() << error;
        emit controlStateChanged(false, error);
        emit errorOccurred(error);
        return;
    }

    // ES8388 exposes the digital PCM level separately from its analogue
    // output gains. Keep both board outputs at a safe -3 dB-ish gain and use
    // PCM as the user-facing 0-100 volume. Optional controls are ignored on
    // other ALSA cards so the adapter remains portable.
    setPlaybackVolume(mixer, "Output 1", 75, false, nullptr);
    setPlaybackVolume(mixer, "Output 2", 75, false, nullptr);
    const bool enabled = bounded > 0;
    setPlaybackSwitch(mixer, m_playbackControlName.constData(), enabled);
    setPlaybackSwitch(mixer, "Left Mixer", enabled);
    setPlaybackSwitch(mixer, "Right Mixer", enabled);
    emit controlStateChanged(true, m_summary);
    emit volumeApplied(bounded);
#else
    Q_UNUSED(bounded)
#endif
}

void AudioVolumeAdapter::publishUnavailable(const QString& detail)
{
    const bool changed = m_available || detail != m_lastUnavailableDetail;
    m_available = false;
    m_summary.clear();
    if (!changed)
        return;
    m_lastUnavailableDetail = detail;
    qWarning() << "Audio volume adapter unavailable:" << detail;
    emit controlStateChanged(false, detail);
}

void AudioVolumeAdapter::scheduleRetry()
{
    if (m_retryTimer.isActive() || m_retryAttempts >= AudioRetryLimit)
        return;
    ++m_retryAttempts;
    m_retryTimer.start();
}
