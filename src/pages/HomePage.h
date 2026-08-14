#pragma once

#include <QWidget>

#include "model/SystemModels.h"

class QPushButton;
class StatusBarWidget;

class HomePage final : public QWidget {
    Q_OBJECT

public:
    explicit HomePage(QWidget* parent = nullptr);
    void setSystemStatus(const SystemStatus& status);

signals:
    void talkRequested();
    void careRequested();
    void reminderRequested();
    void settingsRequested();

private:
    QPushButton* m_talkButton = nullptr;
    QPushButton* m_careButton = nullptr;
    QPushButton* m_reminderButton = nullptr;
    StatusBarWidget* m_statusBar = nullptr;
};
