#pragma once

#include <QDate>
#include <QSqlDatabase>

class CareEventRepository final {
public:
    explicit CareEventRepository(const QSqlDatabase& database);

    bool append(const QString& eventType, int value, const QString& source,
                QString* error = nullptr);
    int sumForDate(const QString& eventType, const QDate& date,
                   QString* error = nullptr) const;

private:
    QSqlDatabase m_database;
};

