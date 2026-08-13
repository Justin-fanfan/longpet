#include "SleepPage.h"

#include "widgets/PetFaceWidget.h"
#include "widgets/VisualComponents.h"

#include <QPushButton>
#include <QVBoxLayout>

SleepPage::SleepPage(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("sleepPage"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 40, 0, 32);
    root->setSpacing(0);
    auto* time = makeLabel(QStringLiteral("23:48"), "sleepTime", this);
    time->setAlignment(Qt::AlignCenter);
    root->addWidget(time);
    auto* date = makeLabel(QStringLiteral("8月11日  星期二"), "sleepText", this);
    date->setAlignment(Qt::AlignCenter);
    root->addWidget(date);
    root->addSpacing(24);
    auto* face = new PetFaceWidget(PetExpression::Sleep, this);
    face->setAnimationEnabled(false);
    face->setFixedSize(440, 240);
    root->addWidget(face, 0, Qt::AlignHCenter);
    root->addStretch();
    auto* hint = makeLabel(QStringLiteral("轻触屏幕唤醒"), "sleepText", this);
    hint->setAlignment(Qt::AlignCenter);
    root->addWidget(hint);

    m_wakeButton = new QPushButton(this);
    m_wakeButton->setProperty("role", "sleepWake");
    m_wakeButton->setGeometry(0, 0, 1024, 600);
    m_wakeButton->setAccessibleName(QStringLiteral("唤醒并返回首页"));
    m_wakeButton->raise();
}

QPushButton* SleepPage::wakeButton() const { return m_wakeButton; }
