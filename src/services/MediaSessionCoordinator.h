#pragma once

#include <QString>

class MediaSessionCoordinator final {
public:
    bool tryAcquire(const QString& owner);
    void release(const QString& owner);
    QString owner() const;

private:
    QString m_owner;
};
