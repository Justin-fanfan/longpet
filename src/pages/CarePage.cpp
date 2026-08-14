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
    root->addWidget(header);
    connect(header, &PageHeaderWidget::backRequested,
            this, &CarePage::backRequested);

    auto* content = new QVBoxLayout;
    content->setContentsMargins(32, 16, 32, 32);
    content->setSpacing(16);

    auto* mainRow = new QHBoxLayout;
    mainRow->setSpacing(24);
    auto* waterCard = new SectionCard(this);
    waterCard->setFixedSize(400, 280);
    auto* waterTop = new QHBoxLayout;
    waterTop->setSpacing(20);
    waterTop->addWidget(new SvgIconWidget(QStringLiteral(":/icons/water.svg"), 72, waterCard));
    auto* waterText = new QVBoxLayout;
    waterText->addWidget(makeLabel(QStringLiteral("今天喝水"), "assist", waterCard));
    m_waterValueLabel = makeLabel({}, "key", waterCard);
    waterText->addWidget(m_waterValueLabel);
    waterTop->addLayout(waterText, 1);
    waterCard->contentLayout()->addLayout(waterTop);
    m_waterProgress = new QProgressBar(waterCard);
    m_waterProgress->setRange(0, 8);
    m_waterProgress->setTextVisible(false);
    waterCard->contentLayout()->addWidget(m_waterProgress);
    m_waterHintLabel = makeLabel({}, "body", waterCard);
    waterCard->contentLayout()->addWidget(m_waterHintLabel);
    m_updatedLabel = makeLabel({}, "assist", waterCard);
    waterCard->contentLayout()->addWidget(m_updatedLabel);
    mainRow->addWidget(waterCard);

    auto* right = new QVBoxLayout;
    right->setSpacing(16);
    auto* medicine = new SectionCard(this);
    medicine->setFixedHeight(128);
    auto* medRow = new QHBoxLayout;
    medRow->setSpacing(18);
    medRow->addWidget(new SvgIconWidget(QStringLiteral(":/icons/pill.svg"), 54, medicine));
    auto* medText = new QVBoxLayout;
    medText->setSpacing(3);
    medText->addWidget(makeLabel(QStringLiteral("今日用药"), "assist", medicine));
    m_medicineValueLabel = makeLabel({}, "body", medicine);
    medText->addWidget(m_medicineValueLabel);
    medText->addWidget(makeLabel(QStringLiteral("数据来自本地提醒完成记录"), "assist", medicine));
    medRow->addLayout(medText, 1);
    medicine->contentLayout()->addLayout(medRow);
    right->addWidget(medicine);

    auto* activity = new SectionCard(this);
    activity->setFixedHeight(136);
    auto* activityRow = new QHBoxLayout;
    activityRow->setSpacing(20);
    activityRow->addWidget(new SvgIconWidget(QStringLiteral(":/icons/activity.svg"), 58, activity));
    auto* activityText = new QVBoxLayout;
    activityText->addWidget(makeLabel(QStringLiteral("今日活动与互动"), "assist", activity));
    m_activityValueLabel = makeLabel({}, "body", activity);
    m_interactionValueLabel = makeLabel({}, "assist", activity);
    activityText->addWidget(m_activityValueLabel);
    activityText->addWidget(m_interactionValueLabel);
    activityRow->addLayout(activityText, 1);
    activity->contentLayout()->addLayout(activityRow);
    right->addWidget(activity);
    mainRow->addLayout(right, 1);
    content->addLayout(mainRow);

    auto* bottom = new QHBoxLayout;
    bottom->addStretch();
    auto* waterButton = new QPushButton(QStringLiteral("我喝了一杯水"), this);
    waterButton->setObjectName(QStringLiteral("recordWaterButton"));
    waterButton->setProperty("role", "primary");
    waterButton->setFixedSize(300, 88);
    auto* reminderButton = new QPushButton(QStringLiteral("查看提醒"), this);
    reminderButton->setObjectName(QStringLiteral("careReminderButton"));
    reminderButton->setProperty("role", "secondary");
    reminderButton->setFixedSize(260, 88);
    bottom->addWidget(waterButton);
    bottom->addSpacing(16);
    bottom->addWidget(reminderButton);
    content->addLayout(bottom);
    root->addLayout(content, 1);

    connect(waterButton, &QPushButton::clicked,
            this, &CarePage::recordWaterRequested);
    connect(reminderButton, &QPushButton::clicked,
            this, &CarePage::reminderRequested);
    setSummary({});
}

void CarePage::setSummary(const CareSummary& summary)
{
    const int goal = qMax(1, summary.waterGoal);
    const int completed = qBound(0, summary.waterCompleted, goal);
    m_waterProgress->setRange(0, goal);
    m_waterProgress->setValue(completed);
    m_waterValueLabel->setText(QStringLiteral("%1 / %2 杯").arg(completed).arg(goal));
    m_waterHintLabel->setText(completed >= goal
        ? QStringLiteral("今天的饮水目标完成啦")
        : QStringLiteral("再喝 %1 杯就完成啦").arg(goal - completed));
    m_medicineValueLabel->setText(summary.medicineTotal == 0
        ? QStringLiteral("今天没有启用的用药提醒")
        : QStringLiteral("已完成 %1 / %2 项").arg(summary.medicineCompleted)
              .arg(summary.medicineTotal));
    m_activityValueLabel->setText(summary.activityMinutes > 0
        ? QStringLiteral("活动：%1 分钟").arg(summary.activityMinutes)
        : QStringLiteral("活动：待设备接入"));
    m_interactionValueLabel->setText(summary.interactionCount > 0
        ? QStringLiteral("互动：%1 次").arg(summary.interactionCount)
        : QStringLiteral("互动：待感知接入"));
    m_updatedLabel->setText(summary.lastUpdated.isValid()
        ? QStringLiteral("更新于 %1").arg(summary.lastUpdated.time().toString(QStringLiteral("HH:mm")))
        : QStringLiteral("尚无本地关怀记录"));
}
