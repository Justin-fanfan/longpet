#pragma once

#include "model/ReminderModels.h"

#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;

class CarePage final : public QWidget {
    Q_OBJECT

public:
    explicit CarePage(QWidget* parent = nullptr);
    void setSummary(const CareSummary& summary);

signals:
    void backRequested();
    void reminderRequested();
    void recordWaterRequested();

private:
    QLabel* m_waterValueLabel = nullptr;
    QLabel* m_waterHintLabel = nullptr;
    QLabel* m_medicineValueLabel = nullptr;
    QLabel* m_activityValueLabel = nullptr;
    QLabel* m_interactionValueLabel = nullptr;
    QLabel* m_updatedLabel = nullptr;
    QProgressBar* m_waterProgress = nullptr;
};
