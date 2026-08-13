#include "ReminderPage.h"

#include "widgets/VisualComponents.h"

#include <QVBoxLayout>

ReminderPage::ReminderPage(QWidget* parent)
    : QWidget(parent)
{
    setProperty("page", true);
    setAttribute(Qt::WA_StyledBackground, true);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto* header = new PageHeaderWidget(QStringLiteral("提醒"), this);
    m_backButton = header->backButton();
    root->addWidget(header);

    auto* content = new QVBoxLayout;
    content->setContentsMargins(128, 16, 128, 32);
    content->setSpacing(8);
    auto* morning = new ReminderItem(QStringLiteral("08:00"), QStringLiteral("早餐后吃药"),
        QStringLiteral(":/icons/pill.svg"), ReminderVisualState::Completed, this);
    auto* noon = new ReminderItem(QStringLiteral("12:00"), QStringLiteral("喝水"),
        QStringLiteral(":/icons/water.svg"), ReminderVisualState::Pending, this);
    auto* evening = new ReminderItem(QStringLiteral("20:00"), QStringLiteral("晚间用药"),
        QStringLiteral(":/icons/pill.svg"), ReminderVisualState::Missed, this);
    content->addWidget(morning);
    content->addWidget(noon);
    content->addWidget(evening);
    content->addStretch();
    m_editButton = new LargeActionButton(QStringLiteral("新建提醒"),
        QStringLiteral(":/icons/plus-dark.svg"), "primary", this);
    m_editButton->setProperty("compact", true);
    m_editButton->setFixedSize(224, 80);
    content->addWidget(m_editButton, 0, Qt::AlignRight);
    connect(morning, &QPushButton::clicked, m_editButton, &QPushButton::click);
    connect(noon, &QPushButton::clicked, m_editButton, &QPushButton::click);
    connect(evening, &QPushButton::clicked, m_editButton, &QPushButton::click);
    root->addLayout(content, 1);
}

QPushButton* ReminderPage::backButton() const { return m_backButton; }
QPushButton* ReminderPage::editButton() const { return m_editButton; }
