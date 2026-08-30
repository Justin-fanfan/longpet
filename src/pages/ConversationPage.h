#pragma once

#include "model/AiModels.h"

#include <QWidget>

class QLabel;
class PetFaceWidget;
class QPushButton;

class ConversationPage final : public QWidget {
    Q_OBJECT

public:
    explicit ConversationPage(QWidget* parent = nullptr);

    void setSnapshot(const VoiceInteractionSnapshot& snapshot);

signals:
    void primaryRequested();
    void secondaryRequested();

private:
    VoiceInteractionSnapshot m_snapshot;
    PetFaceWidget* m_face = nullptr;
    QLabel* m_stateLabel = nullptr;
    QLabel* m_transcriptLabel = nullptr;
    QLabel* m_responseLabel = nullptr;
    QLabel* m_errorLabel = nullptr;
    QPushButton* m_primaryButton = nullptr;
    QPushButton* m_secondaryButton = nullptr;
};
