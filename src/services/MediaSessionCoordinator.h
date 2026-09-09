#pragma once

#include "model/AiModels.h"
#include <QObject>
#include <QTimer>

class KwsPort;

class MediaSessionCoordinator final : public QObject {
    Q_OBJECT
public:
    explicit MediaSessionCoordinator(QObject* parent = nullptr);
    void setKws(KwsPort* kws, const KwsConfiguration& configuration);
    void shutdown();
    bool tryAcquire(const QString& owner);
    bool isReady(const QString& owner) const;
    void release(const QString& owner);
    QString owner() const;

signals:
    void mediaReady(const QString& owner);
    void mediaFailed(const QString& owner, const QString& message);

private:
    void acknowledgePause();
    void scheduleResume();
    QString m_owner;
    KwsPort* m_kws = nullptr;
    QTimer m_pauseDeadline;
    QTimer m_resumeTimer;
    bool m_ready = false;
    bool m_shutdown = false;
};
