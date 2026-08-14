#include "ReminderEditPage.h"

#include "widgets/VisualComponents.h"

#include <QButtonGroup>
#include <QDateEdit>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QTimeEdit>
#include <QVBoxLayout>

namespace {
QHBoxLayout* segmentedRow(const QStringList& items, QButtonGroup* group,
                          const QList<int>& ids, QWidget* parent)
{
    auto* layout = new QHBoxLayout;
    layout->setSpacing(8);
    group->setExclusive(true);
    for (int i = 0; i < items.size(); ++i) {
        auto* button = new QPushButton(items[i], parent);
        button->setProperty("role", "segment");
        button->setCheckable(true);
        group->addButton(button, ids.at(i));
        layout->addWidget(button, 1);
    }
    return layout;
}
}

ReminderEditPage::ReminderEditPage(QWidget* parent)
    : QWidget(parent)
{
    setProperty("page", true);
    setAttribute(Qt::WA_StyledBackground, true);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto* header = new PageHeaderWidget(QStringLiteral("编辑提醒"), this);
    root->addWidget(header);
    connect(header, &PageHeaderWidget::backRequested,
            this, &ReminderEditPage::backRequested);

    auto* content = new QVBoxLayout;
    content->setContentsMargins(32, 12, 32, 24);
    content->setSpacing(8);

    auto* firstRow = new QHBoxLayout;
    firstRow->setSpacing(16);
    auto* timeCard = new SectionCard(this);
    timeCard->setFixedSize(300, 96);
    auto* timeRow = new QHBoxLayout;
    timeRow->addWidget(makeLabel(QStringLiteral("时间"), "assist", timeCard));
    m_timeEdit = new QTimeEdit(timeCard);
    m_timeEdit->setObjectName(QStringLiteral("reminderTimeEdit"));
    m_timeEdit->setDisplayFormat(QStringLiteral("HH:mm"));
    m_timeEdit->setFixedWidth(180);
    timeRow->addWidget(m_timeEdit);
    timeCard->contentLayout()->addLayout(timeRow);
    firstRow->addWidget(timeCard);

    auto* typeCard = new SectionCard(this);
    typeCard->setFixedHeight(96);
    auto* typeRow = new QHBoxLayout;
    typeRow->setSpacing(16);
    typeRow->addWidget(makeLabel(QStringLiteral("类型"), "assist", typeCard));
    m_typeGroup = new QButtonGroup(this);
    typeRow->addLayout(segmentedRow(
        {QStringLiteral("吃药"), QStringLiteral("喝水"), QStringLiteral("其他")},
        m_typeGroup,
        {static_cast<int>(ReminderType::Medicine), static_cast<int>(ReminderType::Water),
         static_cast<int>(ReminderType::Other)}, typeCard), 1);
    typeCard->contentLayout()->addLayout(typeRow);
    firstRow->addWidget(typeCard, 1);
    content->addLayout(firstRow);

    auto* repeatCard = new QFrame(this);
    repeatCard->setProperty("card", true);
    repeatCard->setFixedHeight(72);
    auto* repeatRow = new QHBoxLayout(repeatCard);
    repeatRow->setContentsMargins(24, 4, 8, 4);
    repeatRow->setSpacing(16);
    repeatRow->addWidget(makeLabel(QStringLiteral("重复"), "assist", repeatCard));
    m_repeatGroup = new QButtonGroup(this);
    repeatRow->addLayout(segmentedRow(
        {QStringLiteral("每天"), QStringLiteral("工作日"), QStringLiteral("仅一次")},
        m_repeatGroup,
        {static_cast<int>(ReminderRepeatRule::Daily), static_cast<int>(ReminderRepeatRule::Weekdays),
         static_cast<int>(ReminderRepeatRule::Once)}, repeatCard), 1);
    content->addWidget(repeatCard);

    auto* detailRow = new QHBoxLayout;
    detailRow->setSpacing(16);
    m_titleEdit = new QLineEdit(this);
    m_titleEdit->setObjectName(QStringLiteral("reminderTitleEdit"));
    m_titleEdit->setPlaceholderText(QStringLiteral("提醒内容，例如：晚饭后吃药"));
    m_titleEdit->setMaxLength(40);
    m_titleEdit->setAccessibleName(QStringLiteral("提醒内容"));
    detailRow->addWidget(m_titleEdit, 1);
    m_dateEdit = new QDateEdit(this);
    m_dateEdit->setObjectName(QStringLiteral("reminderDateEdit"));
    m_dateEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    m_dateEdit->setCalendarPopup(true);
    m_dateEdit->setFixedWidth(210);
    detailRow->addWidget(m_dateEdit);
    content->addLayout(detailRow);
    content->addStretch();

    auto* actions = new QHBoxLayout;
    actions->setSpacing(16);
    m_deleteButton = new QPushButton(QStringLiteral("删除"), this);
    m_deleteButton->setObjectName(QStringLiteral("deleteReminderButton"));
    m_deleteButton->setProperty("role", "dangerCompact");
    m_deleteButton->setFixedHeight(88);
    auto* cancelButton = new QPushButton(QStringLiteral("取消"), this);
    cancelButton->setObjectName(QStringLiteral("cancelReminderButton"));
    cancelButton->setProperty("role", "secondary");
    cancelButton->setFixedHeight(88);
    auto* saveButton = new QPushButton(QStringLiteral("保存提醒"), this);
    saveButton->setObjectName(QStringLiteral("saveReminderButton"));
    saveButton->setProperty("role", "primary");
    saveButton->setFixedHeight(88);
    actions->addWidget(m_deleteButton, 2);
    actions->addWidget(cancelButton, 3);
    actions->addWidget(saveButton, 5);
    content->addLayout(actions);
    root->addLayout(content, 1);

    connect(saveButton, &QPushButton::clicked, this, [this] {
        emit saveRequested(currentDraft());
    });
    connect(cancelButton, &QPushButton::clicked,
            this, &ReminderEditPage::cancelRequested);
    connect(m_deleteButton, &QPushButton::clicked, this, [this] {
        if (m_editingId != 0)
            emit deleteRequested(m_editingId);
    });
    connect(m_repeatGroup, &QButtonGroup::idClicked, this, [this](int id) {
        m_dateEdit->setVisible(id == static_cast<int>(ReminderRepeatRule::Once));
    });
    setDraft({});
}

void ReminderEditPage::setDraft(const ReminderDraft& draft)
{
    m_editingId = draft.id;
    m_expectedRevision = draft.expectedRevision;
    m_timeEdit->setTime(draft.timeOfDay.isValid() ? draft.timeOfDay : QTime(8, 0));
    m_dateEdit->setDate(draft.scheduledDate.isValid()
        ? draft.scheduledDate : QDate::currentDate());
    m_titleEdit->setText(draft.title);
    if (QAbstractButton* type = m_typeGroup->button(static_cast<int>(draft.type)))
        type->setChecked(true);
    if (QAbstractButton* repeat = m_repeatGroup->button(static_cast<int>(draft.repeatRule)))
        repeat->setChecked(true);
    m_dateEdit->setVisible(draft.repeatRule == ReminderRepeatRule::Once);
    m_deleteButton->setVisible(m_editingId != 0);
}

ReminderDraft ReminderEditPage::currentDraft() const
{
    ReminderDraft draft;
    draft.id = m_editingId;
    draft.expectedRevision = m_expectedRevision;
    draft.type = static_cast<ReminderType>(checkedId(m_typeGroup,
        static_cast<int>(ReminderType::Medicine)));
    draft.repeatRule = static_cast<ReminderRepeatRule>(checkedId(m_repeatGroup,
        static_cast<int>(ReminderRepeatRule::Daily)));
    draft.timeOfDay = m_timeEdit->time();
    draft.scheduledDate = m_dateEdit->date();
    draft.title = m_titleEdit->text();
    draft.enabled = true;
    return draft;
}

int ReminderEditPage::checkedId(const QButtonGroup* group, int fallback) const
{
    return group->checkedId() < 0 ? fallback : group->checkedId();
}
