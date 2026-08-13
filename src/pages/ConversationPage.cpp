#include "ConversationPage.h"

#include "widgets/PetFaceWidget.h"
#include "widgets/VisualComponents.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace {
PetExpression expressionFor(ConversationMode mode)
{
    switch (mode) {
    case ConversationMode::Listening: return PetExpression::Listening;
    case ConversationMode::Thinking: return PetExpression::Thinking;
    case ConversationMode::Speaking: return PetExpression::Speaking;
    }
    return PetExpression::Default;
}

QString stateTextFor(ConversationMode mode)
{
    switch (mode) {
    case ConversationMode::Listening: return QStringLiteral("我在听");
    case ConversationMode::Thinking: return QStringLiteral("让我想一想……");
    case ConversationMode::Speaking: return QStringLiteral("我在说");
    }
    return {};
}
}

ConversationPage::ConversationPage(ConversationMode mode, QWidget* parent)
    : QWidget(parent), m_mode(mode)
{
    setProperty("page", true);
    setAttribute(Qt::WA_StyledBackground, true);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(32, 24, 32, 24);
    root->setSpacing(0);

    m_nextButton = new QPushButton(this);
    m_nextButton->hide();
    m_backButton = new QPushButton(mode == ConversationMode::Speaking
            ? QStringLiteral("回到陪伴") : QStringLiteral("结束对话"), this);
    m_backButton->setProperty("role", "immersiveAction");
    m_backButton->setFixedSize(176, 64);

    root->addStretch(1);
    auto* face = new PetFaceWidget(expressionFor(mode), this);
    face->setFixedSize(mode == ConversationMode::Speaking ? QSize(600, 250) : QSize(640, 270));
    root->addWidget(face, 0, Qt::AlignHCenter);
    root->addSpacing(8);

    auto* state = makeLabel(stateTextFor(mode), "petState", this);
    state->setAlignment(Qt::AlignCenter);
    state->setFixedHeight(44);
    root->addWidget(state);

    if (mode == ConversationMode::Listening) {
        root->addSpacing(16);
        auto* example = new QFrame(this);
        example->setProperty("card", true);
        example->setFixedSize(704, 88);
        auto* exampleLayout = new QHBoxLayout(example);
        exampleLayout->setContentsMargins(24, 0, 24, 0);
        exampleLayout->setSpacing(16);
        auto* dot = makeLabel(QStringLiteral("●"), "accent", example);
        exampleLayout->addWidget(dot);
        exampleLayout->addWidget(makeLabel(QStringLiteral("“提醒我晚上八点吃药”"), "body", example), 1);
        exampleLayout->addWidget(makeLabel(QStringLiteral("UI 示例"), "assist", example));
        root->addWidget(example, 0, Qt::AlignHCenter);
    } else if (mode == ConversationMode::Thinking) {
        root->addSpacing(12);
        auto* hint = makeLabel(QStringLiteral("正在整理刚才的话   ·  ·  ·"), "assist", this);
        hint->setAlignment(Qt::AlignCenter);
        hint->setFixedHeight(36);
        root->addWidget(hint);
    } else {
        root->addSpacing(12);
        auto* reply = makeLabel(QStringLiteral("好的，晚上八点\n我会提醒您吃药。"),
                                "companionReply", this);
        reply->setAlignment(Qt::AlignCenter);
        reply->setFixedHeight(80);
        root->addWidget(reply);
        auto* hint = makeLabel(QStringLiteral("●  ●●  ●    UI 示例 · 不包含语音播放"), "assist", this);
        hint->setAlignment(Qt::AlignCenter);
        hint->setFixedHeight(32);
        root->addWidget(hint);
    }

    root->addStretch(1);
    root->addWidget(m_backButton, 0, Qt::AlignHCenter);
}

QPushButton* ConversationPage::backButton() const { return m_backButton; }
QPushButton* ConversationPage::nextButton() const { return m_nextButton; }
ConversationMode ConversationPage::mode() const { return m_mode; }
