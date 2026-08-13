#pragma once

#include <QWidget>

class QPushButton;

class ReminderPage final : public QWidget {
public:
    explicit ReminderPage(QWidget* parent = nullptr);
    QPushButton* backButton() const;
    QPushButton* editButton() const;

private:
    QPushButton* m_backButton = nullptr;
    QPushButton* m_editButton = nullptr;
};
