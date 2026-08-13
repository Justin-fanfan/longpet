#include "ReminderEditPage.h"

#include "widgets/VisualComponents.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QVBoxLayout>

namespace {
QHBoxLayout* segmentedRow(const QStringList& items, int checkedIndex, QWidget* parent)
{
    auto* layout = new QHBoxLayout;
    layout->setSpacing(8);
    auto* group = new QButtonGroup(parent);
    group->setExclusive(true);
    for (int i = 0; i < items.size(); ++i) {
        auto* button = new QPushButton(items[i], parent);
        button->setProperty("role", "segment");
        button->setCheckable(true);
        button->setChecked(i == checkedIndex);
        group->addButton(button);
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
    m_backButton = header->backButton();
    root->addWidget(header);

    auto* content = new QVBoxLayout;
    content->setContentsMargins(32, 16, 32, 32);
    content->setSpacing(8);
    auto* firstRow = new QHBoxLayout;
    firstRow->setSpacing(16);
    auto* timeCard = new SectionCard(this);
    timeCard->setFixedSize(300, 96);
    auto* timeRow = new QHBoxLayout;
    timeRow->addWidget(makeLabel(QStringLiteral("时间"), "assist", timeCard));
    timeRow->addStretch();
    timeRow->addWidget(makeLabel(QStringLiteral("20:00"), "key", timeCard));
    timeCard->contentLayout()->addLayout(timeRow);
    firstRow->addWidget(timeCard);

    auto* typeCard = new SectionCard(this);
    typeCard->setFixedHeight(96);
    auto* typeRow = new QHBoxLayout;
    typeRow->setSpacing(16);
    typeRow->addWidget(makeLabel(QStringLiteral("类型"), "assist", typeCard));
    typeRow->addLayout(segmentedRow({QStringLiteral("吃药"), QStringLiteral("喝水"), QStringLiteral("其他")}, 0, typeCard), 1);
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
    repeatRow->addLayout(segmentedRow({QStringLiteral("每天"), QStringLiteral("工作日"), QStringLiteral("仅一次")}, 0, repeatCard), 1);
    content->addWidget(repeatCard);

    auto* noteRow = new QHBoxLayout;
    noteRow->setSpacing(16);
    auto* noteLabel = makeLabel(QStringLiteral("备注"), "assist", this);
    noteLabel->setFixedWidth(64);
    noteRow->addWidget(noteLabel);
    auto* note = new QLineEdit(QStringLiteral("晚饭后"), this);
    note->setAccessibleName(QStringLiteral("提醒备注"));
    noteRow->addWidget(note, 1);
    content->addLayout(noteRow);
    content->addStretch();

    auto* actions = new QHBoxLayout;
    actions->setSpacing(16);
    m_cancelButton = new QPushButton(QStringLiteral("取消"), this);
    m_cancelButton->setProperty("role", "secondary");
    m_cancelButton->setFixedHeight(96);
    m_saveButton = new QPushButton(QStringLiteral("保存提醒"), this);
    m_saveButton->setProperty("role", "primary");
    m_saveButton->setFixedHeight(96);
    actions->addWidget(m_cancelButton, 3);
    actions->addWidget(m_saveButton, 7);
    content->addLayout(actions);
    root->addLayout(content, 1);

    m_toast = new ToastWidget(this);
}

QPushButton* ReminderEditPage::backButton() const { return m_backButton; }
QPushButton* ReminderEditPage::cancelButton() const { return m_cancelButton; }
QPushButton* ReminderEditPage::saveButton() const { return m_saveButton; }
ToastWidget* ReminderEditPage::toast() const { return m_toast; }
