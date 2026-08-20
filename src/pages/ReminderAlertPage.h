#pragma once

#include "model/ReminderModels.h"

#include <QWidget>

class QLabel;
class SvgIconWidget;

class ReminderAlertPage final : public QWidget {
    Q_OBJECT

public:
    explicit ReminderAlertPage(QWidget* parent = nullptr);

    void setPresentation(const ReminderPresentation& presentation);
    void clearPresentation();
    ReminderPresentation currentPresentation() const;
    bool hasPresentation() const;

signals:
    void acknowledgeRequested(ReminderEventId eventId);
    void completeRequested(ReminderEventId eventId);

private:
    ReminderPresentation m_presentation;
    SvgIconWidget* m_icon = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_timeLabel = nullptr;
};
