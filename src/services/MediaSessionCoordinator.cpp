#include "MediaSessionCoordinator.h"
#include "services/KwsPorts.h"
#include <QDebug>

MediaSessionCoordinator::MediaSessionCoordinator(QObject* parent) : QObject(parent)
{
    m_pauseDeadline.setSingleShot(true);
    m_resumeTimer.setSingleShot(true);
    connect(&m_pauseDeadline, &QTimer::timeout, this, [this] {
        if (m_owner.isEmpty() || m_ready)
            return;
        const QString waitingOwner = m_owner;
        qWarning().noquote() << "Audio ownership pause_timeout owner=" << waitingOwner;
        // A timeout is NOT proof that ALSA was released. Fail this operation.
        if (m_kws)
            m_kws->stop();
        emit mediaFailed(waitingOwner, QStringLiteral("麦克风释放超时，请稍后重试"));
    });
    connect(&m_resumeTimer, &QTimer::timeout, this, [this] {
        if (!m_shutdown && m_owner.isEmpty() && m_kws) {
            if (!m_kws->isRunning())
                m_kws->start();
            m_kws->resume();
        }
    });
}

void MediaSessionCoordinator::setKws(KwsPort* kws, const KwsConfiguration& configuration)
{
    m_kws = configuration.enabled ? kws : nullptr;
    m_pauseDeadline.setInterval(qMax(100, configuration.pauseTimeoutMs));
    m_resumeTimer.setInterval(qMax(0, configuration.resumeCooldownMs));
    if (!m_kws)
        return;
    connect(m_kws, &KwsPort::kwsPaused, this, &MediaSessionCoordinator::acknowledgePause);
    connect(m_kws, &KwsPort::kwsReady, this, [this] {
        if (m_owner.isEmpty())
            scheduleResume();
        else
            m_kws->pause();
    });
    connect(m_kws, &KwsPort::kwsStopped, this, [this] {
        // The adapter emits stopped only after the process tree has exited.
        if (m_pauseDeadline.isActive())
            acknowledgePause();
        else if (m_owner.isEmpty())
            scheduleResume();
    });
}

void MediaSessionCoordinator::shutdown()
{
    m_shutdown = true;
    m_pauseDeadline.stop();
    m_resumeTimer.stop();
}

void MediaSessionCoordinator::acknowledgePause()
{
    if (m_owner.isEmpty() || m_ready || m_shutdown)
        return;
    m_pauseDeadline.stop();
    m_ready = true;
    qInfo().noquote() << "Audio ownership ready owner=" << m_owner;
    emit mediaReady(m_owner);
}

void MediaSessionCoordinator::scheduleResume()
{
    if (!m_shutdown && m_owner.isEmpty() && m_kws)
        m_resumeTimer.start();
}

bool MediaSessionCoordinator::tryAcquire(const QString& owner)
{
    if (m_shutdown || owner.isEmpty())
        return false;
    if (!m_owner.isEmpty() && m_owner != owner)
        return false;
    if (m_owner == owner)
        return true;
    m_owner = owner;
    m_resumeTimer.stop();
    m_ready = !m_kws;
    if (m_kws) {
        m_pauseDeadline.start();
        m_kws->pause();
    }
    return true;
}

bool MediaSessionCoordinator::isReady(const QString& owner) const
{
    return m_ready && m_owner == owner;
}

void MediaSessionCoordinator::release(const QString& owner)
{
    if (m_owner == owner) {
        m_owner.clear();
        m_ready = false;
        m_pauseDeadline.stop();
        scheduleResume();
    }
}

QString MediaSessionCoordinator::owner() const
{
    return m_owner;
}
