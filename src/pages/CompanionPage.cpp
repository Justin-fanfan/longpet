#include "CompanionPage.h"

#include "widgets/PetFaceWidget.h"

#include <QPushButton>
#include <QStackedLayout>

CompanionPage::CompanionPage(QWidget* parent)
    : QWidget(parent)
{
    setProperty("page", true);
    setObjectName(QStringLiteral("companionPage"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto* root = new QStackedLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setStackingMode(QStackedLayout::StackAll);

    auto* face = new PetFaceWidget(PetExpression::DefaultOpen, this);
    root->addWidget(face);

    auto* revealButton = new QPushButton(this);
    revealButton->setObjectName(QStringLiteral("companionRevealButton"));
    revealButton->setProperty("role", "companionReveal");
    revealButton->setFocusPolicy(Qt::NoFocus);
    revealButton->setAccessibleName(QStringLiteral("显示控制界面"));
    root->addWidget(revealButton);
    root->setCurrentWidget(revealButton);

    connect(revealButton, &QPushButton::clicked,
            this, &CompanionPage::controlRequested);
}
