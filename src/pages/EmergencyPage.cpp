#include "EmergencyPage.h"

#include "widgets/PetFaceWidget.h"
#include "widgets/VisualComponents.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>

EmergencyPage::EmergencyPage(QWidget* parent)
    : QWidget(parent)
{
    setProperty("page", true);
    setAttribute(Qt::WA_StyledBackground, true);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto* dangerBar = new QFrame(this);
    dangerBar->setObjectName(QStringLiteral("dangerBar"));
    dangerBar->setFixedHeight(8);
    root->addWidget(dangerBar);

    auto* content = new QVBoxLayout;
    content->setContentsMargins(64, 24, 64, 76);
    content->setSpacing(12);
    auto* face = new PetFaceWidget(PetExpression::Alert, this);
    face->setAnimationEnabled(false);
    face->setFixedSize(176, 118);
    content->addWidget(face, 0, Qt::AlignHCenter);

    auto* question = makeLabel(QStringLiteral("您现在感觉还好吗？"), "emergency", this);
    question->setAlignment(Qt::AlignCenter);
    content->addWidget(question);
    m_detailLabel = makeLabel(QStringLiteral("检测到可能发生异常"), "body", this);
    m_detailLabel->setObjectName(QStringLiteral("emergencyDetail"));
    m_detailLabel->setAlignment(Qt::AlignCenter);
    content->addWidget(m_detailLabel);
    auto* answerHint = makeLabel(QStringLiteral("●  请选择一个清楚的回答"), "assist", this);
    answerHint->setAlignment(Qt::AlignCenter);
    answerHint->setFixedHeight(52);
    content->addWidget(answerHint);
    content->addStretch();

    auto* actions = new QHBoxLayout;
    actions->setSpacing(64);
    m_okayButton = new LargeActionButton(QStringLiteral("我没事"),
        QStringLiteral(":/icons/check-dark.svg"), "primary", this);
    m_contactButton = new LargeActionButton(QStringLiteral("联系家人"),
        QStringLiteral(":/icons/phone-dark.svg"), "danger", this);
    m_okayButton->setObjectName(QStringLiteral("emergencyDismissButton"));
    m_contactButton->setObjectName(QStringLiteral("emergencyContactButton"));
    m_okayButton->setFixedHeight(112);
    m_contactButton->setFixedHeight(112);
    actions->addWidget(m_okayButton, 1);
    actions->addWidget(m_contactButton, 1);
    content->addLayout(actions);
    root->addLayout(content, 1);

    m_toast = new ToastWidget(this);
    connect(m_okayButton, &QPushButton::clicked,
            this, &EmergencyPage::dismissRequested);
    connect(m_contactButton, &QPushButton::clicked,
            this, &EmergencyPage::contactRequested);
}

QPushButton* EmergencyPage::okayButton() const { return m_okayButton; }
QPushButton* EmergencyPage::contactButton() const { return m_contactButton; }
ToastWidget* EmergencyPage::toast() const { return m_toast; }

void EmergencyPage::setDetail(const QString& detail)
{
    const QString normalized = detail.simplified();
    m_detailLabel->setText(normalized.isEmpty()
        ? QStringLiteral("检测到可能发生异常") : normalized);
}

QString EmergencyPage::detail() const
{
    return m_detailLabel->text();
}
