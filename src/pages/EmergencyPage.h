#pragma once

#include <QWidget>

class QPushButton;
class ToastWidget;

class EmergencyPage final : public QWidget {
public:
    explicit EmergencyPage(QWidget* parent = nullptr);
    QPushButton* okayButton() const;
    QPushButton* contactButton() const;
    ToastWidget* toast() const;

private:
    QPushButton* m_okayButton = nullptr;
    QPushButton* m_contactButton = nullptr;
    ToastWidget* m_toast = nullptr;
};
