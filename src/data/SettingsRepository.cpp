#include "SettingsRepository.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>

SettingsRepository::SettingsRepository(const QSqlDatabase& database)
    : m_database(database)
{
}

QHash<QString, QString> SettingsRepository::all(QString* error) const
{
    QHash<QString, QString> values;
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("SELECT key,value FROM settings"))) {
        if (error)
            *error = query.lastError().text();
        return values;
    }
    while (query.next())
        values.insert(query.value(0).toString(), query.value(1).toString());
    return values;
}

bool SettingsRepository::setValue(const QString& key, const QString& value, QString* error)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO settings(key,value,updated_at) VALUES(?,?,?) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value,updated_at=excluded.updated_at"));
    query.addBindValue(key);
    query.addBindValue(value);
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (query.exec())
        return true;
    if (error)
        *error = query.lastError().text();
    return false;
}
