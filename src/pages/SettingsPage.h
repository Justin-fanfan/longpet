#pragma once

#include <QWidget>

class QPushButton;
class ToastWidget;

class SettingsPage final : public QWidget {
public:
    explicit SettingsPage(QWidget* parent = nullptr);
    QPushButton* backButton() const;

private:
    QPushButton* m_backButton = nullptr;
    ToastWidget* m_toast = nullptr;
};
