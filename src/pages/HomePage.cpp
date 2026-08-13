#include "HomePage.h"

#include "widgets/PetFaceWidget.h"
#include "widgets/VisualComponents.h"

#include <QHBoxLayout>
#include <QVBoxLayout>

HomePage::HomePage(QWidget* parent)
    : QWidget(parent)
{
    setProperty("page", true);
    setObjectName(QStringLiteral("appRoot"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 32);
    root->setSpacing(0);

    auto* status = new StatusBarWidget(true, this);
    root->addWidget(status);
    root->addSpacing(8);

    auto* face = new PetFaceWidget(PetExpression::DefaultOpen, this);
    face->setObjectName(QStringLiteral("homePetFace"));
    face->setFixedHeight(292);
    root->addWidget(face);

    auto* message = makeLabel(QStringLiteral("你好，我一直在呢"), "petState", this);
    message->setAlignment(Qt::AlignCenter);
    message->setFixedHeight(56);
    root->addWidget(message);
    root->addStretch(1);

    auto* buttons = new QHBoxLayout;
    buttons->setContentsMargins(32, 0, 32, 0);
    buttons->setSpacing(16);
    m_talkButton = new LargeActionButton(QStringLiteral("陪我说话"),
        QStringLiteral(":/icons/microphone-dark.svg"), "primary", this);
    m_careButton = new LargeActionButton(QStringLiteral("今日关怀"),
        QStringLiteral(":/icons/care.svg"), "secondary", this);
    m_reminderButton = new LargeActionButton(QStringLiteral("提醒"),
        QStringLiteral(":/icons/reminder.svg"), "secondary", this);
    m_talkButton->setObjectName(QStringLiteral("talkButton"));
    m_careButton->setObjectName(QStringLiteral("careButton"));
    m_reminderButton->setObjectName(QStringLiteral("reminderButton"));
    m_talkButton->setFixedHeight(96);
    m_careButton->setFixedHeight(96);
    m_reminderButton->setFixedHeight(96);
    buttons->addWidget(m_talkButton, 1);
    buttons->addWidget(m_careButton, 1);
    buttons->addWidget(m_reminderButton, 1);
    root->addLayout(buttons);

    connect(m_talkButton, &QPushButton::clicked,
            this, &HomePage::talkRequested);
    connect(m_careButton, &QPushButton::clicked,
            this, &HomePage::careRequested);
    connect(m_reminderButton, &QPushButton::clicked,
            this, &HomePage::reminderRequested);
    connect(status->settingsButton(), &QPushButton::clicked,
            this, &HomePage::settingsRequested);
}
