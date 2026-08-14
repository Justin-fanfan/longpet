#include "CareEventRepository.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>

CareEventRepository::CareEventRepository(const QSqlDatabase& database)
    : m_database(database)
{
}

bool CareEventRepository::append(const QString& eventType, int value,
                                 const QString& source, QString* error)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO care_events(event_type,value,source,created_at) VALUES(?,?,?,?)"));
    query.addBindValue(eventType);
    query.addBindValue(value);
    query.addBindValue(source);
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (query.exec())
        return true;
    if (error)
        *error = query.lastError().text();
    return false;
}

int CareEventRepository::sumForDate(const QString& eventType, const QDate& date,
                                    QString* error) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT COALESCE(SUM(value),0) FROM care_events "
        "WHERE event_type=? AND created_at>=? AND created_at<?"));
    query.addBindValue(eventType);
    query.addBindValue(QDateTime(date.startOfDay()).toUTC().toString(Qt::ISODate));
    query.addBindValue(QDateTime(date.addDays(1).startOfDay()).toUTC().toString(Qt::ISODate));
    if (!query.exec() || !query.next()) {
        if (error)
            *error = query.lastError().text();
        return 0;
    }
    return query.value(0).toInt();
}

