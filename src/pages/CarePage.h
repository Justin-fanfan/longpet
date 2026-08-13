#pragma once

#include <QWidget>

class QPushButton;

class CarePage final : public QWidget {
public:
    explicit CarePage(QWidget* parent = nullptr);
    QPushButton* backButton() const;
    QPushButton* reminderButton() const;

private:
    QPushButton* m_backButton = nullptr;
    QPushButton* m_reminderButton = nullptr;
};
