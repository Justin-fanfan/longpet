#pragma once

#include <QWidget>

class QPushButton;
class ToastWidget;

class ReminderEditPage final : public QWidget {
public:
    explicit ReminderEditPage(QWidget* parent = nullptr);
    QPushButton* backButton() const;
    QPushButton* cancelButton() const;
    QPushButton* saveButton() const;
    ToastWidget* toast() const;

private:
    QPushButton* m_backButton = nullptr;
    QPushButton* m_cancelButton = nullptr;
    QPushButton* m_saveButton = nullptr;
    ToastWidget* m_toast = nullptr;
};
