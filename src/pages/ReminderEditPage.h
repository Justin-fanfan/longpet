#pragma once

#include "model/ReminderModels.h"

#include <QWidget>

class QButtonGroup;
class QDateEdit;
class QLineEdit;
class QTimeEdit;

class ReminderEditPage final : public QWidget {
    Q_OBJECT

public:
    explicit ReminderEditPage(QWidget* parent = nullptr);
    void setDraft(const ReminderDraft& draft);

signals:
    void saveRequested(const ReminderDraft& draft);
    void deleteRequested(ReminderId id);
    void cancelRequested();
    void backRequested();

private:
    ReminderDraft currentDraft() const;
    int checkedId(const QButtonGroup* group, int fallback) const;

    ReminderDraft m_sourceDraft;
    ReminderId m_editingId = 0;
    int m_expectedRevision = 0;
    QTimeEdit* m_timeEdit = nullptr;
    QDateEdit* m_dateEdit = nullptr;
    QLineEdit* m_titleEdit = nullptr;
    QButtonGroup* m_typeGroup = nullptr;
    QButtonGroup* m_repeatGroup = nullptr;
    class QPushButton* m_deleteButton = nullptr;
};
