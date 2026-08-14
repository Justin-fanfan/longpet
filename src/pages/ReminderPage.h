#pragma once

#include "model/ReminderModels.h"

#include <QWidget>

class QVBoxLayout;
class QPushButton;

class ReminderPage final : public QWidget {
    Q_OBJECT

public:
    explicit ReminderPage(QWidget* parent = nullptr);
    void setReminders(const QList<Reminder>& reminders);

signals:
    void backRequested();
    void addReminderRequested();
    void editReminderRequested(ReminderId id);
    void completeReminderRequested(ReminderId id);

private:
    QVBoxLayout* m_listLayout = nullptr;
};
