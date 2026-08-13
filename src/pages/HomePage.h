#pragma once

#include <QWidget>

class QPushButton;

class HomePage final : public QWidget {
    Q_OBJECT

public:
    explicit HomePage(QWidget* parent = nullptr);

signals:
    void talkRequested();
    void careRequested();
    void reminderRequested();
    void settingsRequested();

private:
    QPushButton* m_talkButton = nullptr;
    QPushButton* m_careButton = nullptr;
    QPushButton* m_reminderButton = nullptr;
};
