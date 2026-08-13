#pragma once

#include <QWidget>

class QPushButton;

enum class ConversationMode { Listening, Thinking, Speaking };

class ConversationPage final : public QWidget {
public:
    explicit ConversationPage(ConversationMode mode, QWidget* parent = nullptr);

    QPushButton* backButton() const;
    QPushButton* nextButton() const;
    ConversationMode mode() const;

private:
    ConversationMode m_mode;
    QPushButton* m_backButton = nullptr;
    QPushButton* m_nextButton = nullptr;
};
