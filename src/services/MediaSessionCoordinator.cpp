#include "MediaSessionCoordinator.h"

bool MediaSessionCoordinator::tryAcquire(const QString& owner)
{
    if (owner.isEmpty())
        return false;
    if (!m_owner.isEmpty() && m_owner != owner)
        return false;
    m_owner = owner;
    return true;
}

void MediaSessionCoordinator::release(const QString& owner)
{
    if (m_owner == owner)
        m_owner.clear();
}

QString MediaSessionCoordinator::owner() const
{
    return m_owner;
}
