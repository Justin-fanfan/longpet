#include "CarePage.h"

#include "widgets/VisualComponents.h"

#include <QHBoxLayout>
#include <QProgressBar>
#include <QVBoxLayout>

CarePage::CarePage(QWidget* parent)
    : QWidget(parent)
{
    setProperty("page", true);
    setAttribute(Qt::WA_StyledBackground, true);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto* header = new PageHeaderWidget(QStringLiteral("今日关怀"), this);
    m_backButton = header->backButton();
    root->addWidget(header);

    auto* content = new QVBoxLayout;
    content->setContentsMargins(32, 16, 32, 32);
    content->setSpacing(16);

    auto* mainRow = new QHBoxLayout;
    mainRow->setSpacing(24);
    auto* waterCard = new SectionCard(this);
    waterCard->setFixedSize(400, 296);
    auto* waterTop = new QHBoxLayout;
    waterTop->setSpacing(20);
    waterTop->addWidget(new SvgIconWidget(QStringLiteral(":/icons/water.svg"), 72, waterCard));
    auto* waterText = new QVBoxLayout;
    waterText->addWidget(makeLabel(QStringLiteral("今天喝水"), "assist", waterCard));
    waterText->addWidget(makeLabel(QStringLiteral("5 / 8 杯"), "key", waterCard));
    waterTop->addLayout(waterText, 1);
    waterCard->contentLayout()->addLayout(waterTop);
    auto* progress = new QProgressBar(waterCard);
    progress->setRange(0, 8);
    progress->setValue(5);
    progress->setTextVisible(false);
    waterCard->contentLayout()->addWidget(progress);
    waterCard->contentLayout()->addWidget(makeLabel(QStringLiteral("再喝三杯就完成啦"), "body", waterCard));
    waterCard->contentLayout()->addWidget(makeLabel(QStringLiteral("慢慢来，记得少量多次。"), "assist", waterCard));
    mainRow->addWidget(waterCard);

    auto* right = new QVBoxLayout;
    right->setSpacing(16);
    auto* medicine = new SectionCard(this);
    medicine->setFixedHeight(136);
    auto* medRow = new QHBoxLayout;
    medRow->setSpacing(18);
    medRow->addWidget(new SvgIconWidget(QStringLiteral(":/icons/pill.svg"), 54, medicine));
    auto* medText = new QVBoxLayout;
    medText->setSpacing(3);
    medText->addWidget(makeLabel(QStringLiteral("今日用药"), "assist", medicine));
    auto* morning = new QHBoxLayout;
    morning->addWidget(makeLabel(QStringLiteral("晨间用药"), "body", medicine));
    morning->addStretch();
    morning->addWidget(new SvgIconWidget(QStringLiteral(":/icons/check.svg"), 28, medicine));
    morning->addWidget(makeLabel(QStringLiteral("已完成"), "success", medicine));
    medText->addLayout(morning);
    medText->addWidget(makeLabel(QStringLiteral("晚间用药　待完成"), "assist", medicine));
    medRow->addLayout(medText, 1);
    medicine->contentLayout()->addLayout(medRow);
    right->addWidget(medicine);

    auto* activity = new SectionCard(this);
    activity->setFixedHeight(144);
    auto* activityRow = new QHBoxLayout;
    activityRow->setSpacing(20);
    activityRow->addWidget(new SvgIconWidget(QStringLiteral(":/icons/activity.svg"), 58, activity));
    auto* activityText = new QVBoxLayout;
    activityText->addWidget(makeLabel(QStringLiteral("今天也有好好活动"), "assist", activity));
    auto* values = new QHBoxLayout;
    values->addWidget(makeLabel(QStringLiteral("43 分钟"), "body", activity));
    values->addStretch();
    values->addWidget(makeLabel(QStringLiteral("今天见到您 6 次"), "assist", activity));
    activityText->addLayout(values);
    activityRow->addLayout(activityText, 1);
    activity->contentLayout()->addLayout(activityRow);
    right->addWidget(activity);
    mainRow->addLayout(right, 1);
    content->addLayout(mainRow);

    auto* bottom = new QHBoxLayout;
    bottom->addStretch();
    m_reminderButton = new QPushButton(QStringLiteral("查看提醒"), this);
    m_reminderButton->setProperty("role", "secondary");
    m_reminderButton->setFixedSize(260, 88);
    bottom->addWidget(m_reminderButton);
    content->addLayout(bottom);
    root->addLayout(content, 1);
}

QPushButton* CarePage::backButton() const { return m_backButton; }
QPushButton* CarePage::reminderButton() const { return m_reminderButton; }
