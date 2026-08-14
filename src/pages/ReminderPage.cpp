#include "ReminderPage.h"

#include "widgets/VisualComponents.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {
QString iconFor(ReminderType type)
{
    switch (type) {
    case ReminderType::Medicine: return QStringLiteral(":/icons/pill.svg");
    case ReminderType::Water: return QStringLiteral(":/icons/water.svg");
    case ReminderType::Other: return QStringLiteral(":/icons/reminder.svg");
    }
    return QStringLiteral(":/icons/reminder.svg");
}

ReminderVisualState visualStateFor(ReminderOccurrenceStatus status)
{
    switch (status) {
    case ReminderOccurrenceStatus::Completed: return ReminderVisualState::Completed;
    case ReminderOccurrenceStatus::Missed: return ReminderVisualState::Missed;
    case ReminderOccurrenceStatus::Disabled: return ReminderVisualState::Disabled;
    case ReminderOccurrenceStatus::Pending: return ReminderVisualState::Pending;
    }
    return ReminderVisualState::Pending;
}
}

ReminderPage::ReminderPage(QWidget* parent)
    : QWidget(parent)
{
    setProperty("page", true);
    setAttribute(Qt::WA_StyledBackground, true);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto* header = new PageHeaderWidget(QStringLiteral("提醒"), this);
    root->addWidget(header);
    connect(header, &PageHeaderWidget::backRequested,
            this, &ReminderPage::backRequested);

    auto* content = new QVBoxLayout;
    content->setContentsMargins(128, 16, 128, 32);
    content->setSpacing(8);
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("reminderScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* listHost = new QWidget(scroll);
    listHost->setProperty("page", true);
    m_listLayout = new QVBoxLayout(listHost);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(8);
    m_listLayout->addStretch();
    scroll->setWidget(listHost);
    content->addWidget(scroll, 1);
    auto* addButton = new LargeActionButton(QStringLiteral("新建提醒"),
        QStringLiteral(":/icons/plus-dark.svg"), "primary", this);
    addButton->setObjectName(QStringLiteral("addReminderButton"));
    addButton->setProperty("compact", true);
    addButton->setFixedSize(224, 80);
    content->addWidget(addButton, 0, Qt::AlignRight);
    connect(addButton, &QPushButton::clicked,
            this, &ReminderPage::addReminderRequested);
    root->addLayout(content, 1);
    setReminders({});
}

void ReminderPage::setReminders(const QList<Reminder>& reminders)
{
    while (QLayoutItem* item = m_listLayout->takeAt(0)) {
        if (QWidget* widget = item->widget())
            widget->deleteLater();
        delete item;
    }

    if (reminders.isEmpty()) {
        auto* empty = makeLabel(QStringLiteral("还没有提醒，点击右下角新建一个吧"),
                                "assist", this);
        empty->setAlignment(Qt::AlignCenter);
        empty->setFixedHeight(160);
        m_listLayout->addWidget(empty);
        m_listLayout->addStretch();
        return;
    }

    for (const Reminder& reminder : reminders) {
        auto* rowHost = new QWidget(this);
        auto* row = new QHBoxLayout(rowHost);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(8);
        auto* item = new ReminderItem(reminder.timeOfDay.toString(QStringLiteral("HH:mm")),
                                      reminder.title, iconFor(reminder.type),
                                      visualStateFor(reminder.status), rowHost);
        item->setObjectName(QStringLiteral("reminderItem_%1").arg(reminder.id));
        const QDate today = QDate::currentDate();
        const bool appliesToday = reminder.repeatRule == ReminderRepeatRule::Daily
            || (reminder.repeatRule == ReminderRepeatRule::Weekdays
                && today.dayOfWeek() >= Qt::Monday && today.dayOfWeek() <= Qt::Friday)
            || (reminder.repeatRule == ReminderRepeatRule::Once
                && reminder.scheduledDate == today);
        const bool canComplete = reminder.enabled && appliesToday
            && reminder.status != ReminderOccurrenceStatus::Completed;
        item->setAccessibleDescription(QStringLiteral("轻触编辑提醒"));
        connect(item, &QPushButton::clicked, this, [this, reminder] {
            emit editReminderRequested(reminder.id);
        });
        row->addWidget(item, 1);

        auto* complete = new QPushButton(
            reminder.status == ReminderOccurrenceStatus::Completed
                ? QStringLiteral("已完成") : QStringLiteral("完成"), rowHost);
        complete->setObjectName(QStringLiteral("completeReminder_%1").arg(reminder.id));
        complete->setProperty("role", canComplete ? "primaryCompact" : "secondaryCompact");
        complete->setFixedSize(112, 80);
        complete->setEnabled(canComplete);
        connect(complete, &QPushButton::clicked, this, [this, reminder] {
            emit completeReminderRequested(reminder.id);
        });
        row->addWidget(complete);
        m_listLayout->addWidget(rowHost);
    }
    m_listLayout->addStretch();
}
