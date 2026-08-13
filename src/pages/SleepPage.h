#pragma once

#include <QWidget>

class QPushButton;

class SleepPage final : public QWidget {
public:
    explicit SleepPage(QWidget* parent = nullptr);
    QPushButton* wakeButton() const;

private:
    QPushButton* m_wakeButton = nullptr;
};
