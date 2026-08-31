#pragma once

#include "model/AiModels.h"

#include <QObject>

class ReminderService;

class VoiceToolRegistry final : public QObject {
    Q_OBJECT

public:
    explicit VoiceToolRegistry(ReminderService* reminderService,
                               QObject* parent = nullptr);

    QList<AiToolDefinition> definitions() const;
    AiToolExecutionResult execute(const AiToolCall& call);

signals:
    void pageRequested(const QString& page);

private:
    AiToolExecutionResult createReminder(const QJsonObject& arguments);
    AiToolExecutionResult listReminders() const;
    AiToolExecutionResult deleteReminder(const QJsonObject& arguments);
    AiToolExecutionResult currentTime() const;
    AiToolExecutionResult openPage(const QJsonObject& arguments);

    ReminderService* m_reminderService = nullptr;
};
