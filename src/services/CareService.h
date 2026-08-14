#pragma once

#include "model/ReminderModels.h"

#include <QObject>

class CareEventRepository;
class ReminderService;

class CareService final : public QObject {
    Q_OBJECT

public:
    explicit CareService(CareEventRepository* repository,
                         ReminderService* reminderService,
                         QObject* parent = nullptr);

    CareSummary todaySummary(QString* error = nullptr) const;
    ServiceResult recordWater();
    ServiceResult recordActivityMinutes(int minutes, const QString& source);
    ServiceResult recordInteraction(int count, const QString& source);

signals:
    void summaryChanged();
    void errorOccurred(const QString& message);

private:
    CareEventRepository* m_repository = nullptr;
    ReminderService* m_reminderService = nullptr;
};
