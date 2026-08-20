#include "ReminderAlertPage.h"

#include "widgets/VisualComponents.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

ReminderAlertPage::ReminderAlertPage(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("reminderAlertPage"));
    setProperty("page", true);
    setAttribute(Qt::WA_StyledBackground, true);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(48, 24, 48, 24);
    root->setSpacing(10);

    auto* heading = makeLabel(QStringLiteral("提醒时间到了"),
                              "reminderAlertHeading", this);
    heading->setObjectName(QStringLiteral("reminderAlertHeading"));
    heading->setAlignment(Qt::AlignCenter);
    root->addWidget(heading);
    root->addStretch(1);

    m_icon = new SvgIconWidget(QStringLiteral(":/icons/reminder.svg"), 112, this);
    m_icon->setObjectName(QStringLiteral("reminderAlertIcon"));
    root->addWidget(m_icon, 0, Qt::AlignHCenter);

    m_titleLabel = makeLabel(QStringLiteral("日常提醒"),
                             "reminderAlertText", this);
    m_titleLabel->setObjectName(QStringLiteral("reminderAlertText"));
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setWordWrap(true);
    m_titleLabel->setMaximumHeight(116);
    root->addWidget(m_titleLabel);

    m_timeLabel = makeLabel({}, "reminderAlertTime", this);
    m_timeLabel->setObjectName(QStringLiteral("reminderAlertTime"));
    m_timeLabel->setAlignment(Qt::AlignCenter);
    root->addWidget(m_timeLabel);
    root->addStretch(1);

    auto* actions = new QHBoxLayout;
    actions->setSpacing(16);
    auto* completeButton = new QPushButton(QStringLiteral("已经完成"), this);
    completeButton->setObjectName(QStringLiteral("completeAlertButton"));
    completeButton->setProperty("role", "reminderComplete");
    completeButton->setFixedSize(280, 96);
    completeButton->setAccessibleName(QStringLiteral("我已经完成了提醒事项"));
    auto* acknowledgeButton = new QPushButton(QStringLiteral("知道了"), this);
    acknowledgeButton->setObjectName(QStringLiteral("acknowledgeAlertButton"));
    acknowledgeButton->setProperty("role", "reminderAcknowledge");
    acknowledgeButton->setFixedHeight(96);
    acknowledgeButton->setAccessibleName(QStringLiteral("我已看到提醒"));
    actions->addWidget(completeButton);
    actions->addWidget(acknowledgeButton, 1);
    root->addLayout(actions);

    auto* hint = makeLabel(QStringLiteral("请点击按钮确认，未确认时稍后会再次提醒"),
                           "reminderAlertHint", this);
    hint->setAlignment(Qt::AlignCenter);
    root->addWidget(hint);

    connect(acknowledgeButton, &QPushButton::clicked, this, [this] {
        if (m_presentation.occurrence.id != 0)
            emit acknowledgeRequested(m_presentation.occurrence.id);
    });
    connect(completeButton, &QPushButton::clicked, this, [this] {
        if (m_presentation.occurrence.id != 0)
            emit completeRequested(m_presentation.occurrence.id);
    });
}

void ReminderAlertPage::setPresentation(const ReminderPresentation& presentation)
{
    m_presentation = presentation;
    m_icon->setResourcePath(reminderIconResourcePath(presentation.reminder.iconKey));
    m_titleLabel->setText(presentation.reminder.title);
    m_timeLabel->setText(QStringLiteral("%1  ·  第 %2 次提醒")
        .arg(presentation.occurrence.scheduledAt.time().toString(QStringLiteral("HH:mm")))
        .arg(presentation.occurrence.presentationCount));
    setAccessibleName(QStringLiteral("提醒：%1").arg(presentation.reminder.title));
}

void ReminderAlertPage::clearPresentation()
{
    m_presentation = {};
}

ReminderPresentation ReminderAlertPage::currentPresentation() const
{
    return m_presentation;
}

bool ReminderAlertPage::hasPresentation() const
{
    return m_presentation.occurrence.id != 0;
}
