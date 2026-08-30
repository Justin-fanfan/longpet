#include "ConversationPage.h"

#include "widgets/PetFaceWidget.h"
#include "widgets/VisualComponents.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
PetExpression expressionFor(VoiceInteractionState state)
{
    switch (state) {
    case VoiceInteractionState::Listening: return PetExpression::Listening;
    case VoiceInteractionState::Recognizing:
    case VoiceInteractionState::Thinking: return PetExpression::Thinking;
    case VoiceInteractionState::Speaking: return PetExpression::Speaking;
    case VoiceInteractionState::Error:
    case VoiceInteractionState::Offline: return PetExpression::Worried;
    case VoiceInteractionState::Cancelled: return PetExpression::DefaultOpen;
    case VoiceInteractionState::Idle: return PetExpression::DefaultOpen;
    }
    return PetExpression::DefaultOpen;
}

QString fallbackStatus(VoiceInteractionState state)
{
    switch (state) {
    case VoiceInteractionState::Idle: return QStringLiteral("可以继续和我说话");
    case VoiceInteractionState::Listening: return QStringLiteral("正在聆听");
    case VoiceInteractionState::Recognizing: return QStringLiteral("正在识别");
    case VoiceInteractionState::Thinking: return QStringLiteral("正在思考");
    case VoiceInteractionState::Speaking: return QStringLiteral("正在回答");
    case VoiceInteractionState::Error: return QStringLiteral("这次没有完成");
    case VoiceInteractionState::Offline: return QStringLiteral("网络暂时不可用");
    case VoiceInteractionState::Cancelled: return QStringLiteral("已停止本次对话");
    }
    return {};
}
}

ConversationPage::ConversationPage(QWidget* parent)
    : QWidget(parent)
{
    setProperty("page", true);
    setObjectName(QStringLiteral("conversationPage"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(32, 20, 32, 24);
    root->setSpacing(8);

    m_face = new PetFaceWidget(PetExpression::Listening, this);
    m_face->setObjectName(QStringLiteral("conversationPetFace"));
    m_face->setMinimumHeight(245);
    root->addWidget(m_face, 1);

    m_stateLabel = makeLabel(QStringLiteral("正在聆听"), "petState", this);
    m_stateLabel->setObjectName(QStringLiteral("conversationState"));
    m_stateLabel->setAlignment(Qt::AlignCenter);
    m_stateLabel->setFixedHeight(48);
    root->addWidget(m_stateLabel);

    m_transcriptLabel = makeLabel({}, "body", this);
    m_transcriptLabel->setObjectName(QStringLiteral("conversationTranscript"));
    m_transcriptLabel->setAlignment(Qt::AlignCenter);
    m_transcriptLabel->setWordWrap(true);
    m_transcriptLabel->setMaximumHeight(58);
    root->addWidget(m_transcriptLabel);

    m_responseLabel = makeLabel({}, "companionReply", this);
    m_responseLabel->setObjectName(QStringLiteral("conversationResponse"));
    m_responseLabel->setAlignment(Qt::AlignCenter);
    m_responseLabel->setWordWrap(true);
    m_responseLabel->setMaximumHeight(86);
    root->addWidget(m_responseLabel);

    m_errorLabel = makeLabel({}, "assist", this);
    m_errorLabel->setObjectName(QStringLiteral("conversationError"));
    m_errorLabel->setAlignment(Qt::AlignCenter);
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setStyleSheet(QStringLiteral("color: #d85b4a; font-weight: 700;"));
    m_errorLabel->setMaximumHeight(54);
    root->addWidget(m_errorLabel);

    auto* actions = new QHBoxLayout;
    actions->setSpacing(18);
    actions->addStretch(1);
    m_secondaryButton = new QPushButton(QStringLiteral("停止"), this);
    m_secondaryButton->setObjectName(QStringLiteral("conversationSecondaryButton"));
    m_secondaryButton->setProperty("role", "secondary");
    m_secondaryButton->setFixedSize(220, 72);
    m_primaryButton = new QPushButton(QStringLiteral("我说完了"), this);
    m_primaryButton->setObjectName(QStringLiteral("conversationPrimaryButton"));
    m_primaryButton->setProperty("role", "primary");
    m_primaryButton->setFixedSize(260, 72);
    actions->addWidget(m_secondaryButton);
    actions->addWidget(m_primaryButton);
    actions->addStretch(1);
    root->addLayout(actions);

    connect(m_primaryButton, &QPushButton::clicked,
            this, &ConversationPage::primaryRequested);
    connect(m_secondaryButton, &QPushButton::clicked,
            this, &ConversationPage::secondaryRequested);
    setSnapshot({});
}

void ConversationPage::setSnapshot(const VoiceInteractionSnapshot& snapshot)
{
    m_snapshot = snapshot;
    m_face->setExpression(expressionFor(snapshot.state));
    m_face->setAnimationEnabled(snapshot.isActive());
    m_stateLabel->setText(snapshot.statusMessage.isEmpty()
        ? fallbackStatus(snapshot.state) : snapshot.statusMessage);

    m_transcriptLabel->setText(snapshot.transcript.isEmpty()
        ? QString() : QStringLiteral("我说：%1").arg(snapshot.transcript));
    m_transcriptLabel->setVisible(!snapshot.transcript.isEmpty());
    m_responseLabel->setText(snapshot.response.isEmpty()
        ? QString() : QStringLiteral("LongPet：%1").arg(snapshot.response));
    m_responseLabel->setVisible(!snapshot.response.isEmpty());
    m_errorLabel->setText(snapshot.errorMessage);
    m_errorLabel->setVisible(!snapshot.errorMessage.isEmpty());

    const bool recording = snapshot.state == VoiceInteractionState::Listening;
    const bool restartable = snapshot.state == VoiceInteractionState::Idle
        || snapshot.state == VoiceInteractionState::Error
        || snapshot.state == VoiceInteractionState::Offline
        || snapshot.state == VoiceInteractionState::Cancelled;
    m_primaryButton->setVisible(snapshot.isActive() || restartable);
    m_primaryButton->setText(recording ? QStringLiteral("我说完了")
        : snapshot.isActive() ? QStringLiteral("重新说话")
        : snapshot.state == VoiceInteractionState::Error
            || snapshot.state == VoiceInteractionState::Offline
            ? QStringLiteral("再试一次") : QStringLiteral("继续说话"));
    m_secondaryButton->setText(snapshot.isActive()
        ? QStringLiteral("停止") : QStringLiteral("返回首页"));
}
