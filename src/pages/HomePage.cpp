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

    m_statusBar = new StatusBarWidget(true, this);
    root->addWidget(m_statusBar);
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
    m_videoCallButton = new LargeActionButton(QStringLiteral("视频通话"),
        QStringLiteral(":/icons/video-call.svg"), "secondary", this);
    m_talkButton->setObjectName(QStringLiteral("talkButton"));
    m_careButton->setObjectName(QStringLiteral("careButton"));
    m_videoCallButton->setObjectName(QStringLiteral("videoCallButton"));
    m_talkButton->setFixedHeight(96);
    m_careButton->setFixedHeight(96);
    m_videoCallButton->setFixedHeight(96);
    buttons->addWidget(m_talkButton, 1);
    buttons->addWidget(m_careButton, 1);
    buttons->addWidget(m_videoCallButton, 1);
    root->addLayout(buttons);

    connect(m_talkButton, &QPushButton::clicked,
            this, &HomePage::talkRequested);
    connect(m_careButton, &QPushButton::clicked,
            this, &HomePage::careRequested);
    connect(m_videoCallButton, &QPushButton::clicked,
            this, &HomePage::videoCallRequested);
    connect(m_statusBar, &StatusBarWidget::settingsRequested,
            this, &HomePage::settingsRequested);
}

void HomePage::setSystemStatus(const SystemStatus& status)
{
    m_statusBar->setStatus(status);
}
