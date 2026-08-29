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
    if (!query.exec(QStringLiteral(
            "SELECT key,value FROM settings WHERE key <> 'settings_revision'"))) {
        if (error)
            *error = query.lastError().text();
        return values;
    }
    while (query.next())
        values.insert(query.value(0).toString(), query.value(1).toString());
    return values;
}

int SettingsRepository::revision(QString* error) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT value FROM settings WHERE key='settings_revision'"));
    if (!query.exec()) {
        if (error)
            *error = query.lastError().text();
        return 0;
    }
    if (!query.next())
        return 0;
    bool valid = false;
    const int value = query.value(0).toInt(&valid);
    return valid && value >= 0 ? value : 0;
}

bool SettingsRepository::setValue(const QString& key, const QString& value, QString* error)
{
    const SettingsRepositoryWriteResult result = setValues({{key, value}});
    if (!result.success && error)
        *error = result.error;
    return result.success;
}

SettingsRepositoryWriteResult SettingsRepository::setValues(
    const QHash<QString, QString>& values, int expectedRevision)
{
    SettingsRepositoryWriteResult result;
    if (!m_database.transaction()) {
        result.error = m_database.lastError().text();
        return result;
    }

    QString revisionError;
    const int currentRevision = revision(&revisionError);
    if (!revisionError.isEmpty()) {
        m_database.rollback();
        result.error = revisionError;
        return result;
    }
    if (expectedRevision >= 0 && expectedRevision != currentRevision) {
        m_database.rollback();
        result.revisionConflict = true;
        result.revision = currentRevision;
        result.error = QStringLiteral("设置已被其他操作修改，请刷新后重试");
        return result;
    }

    const QString updatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO settings(key,value,updated_at) VALUES(?,?,?) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value,updated_at=excluded.updated_at"));
    auto writeValue = [&](const QString& key, const QString& value) {
        query.bindValue(0, key);
        query.bindValue(1, value);
        query.bindValue(2, updatedAt);
        return query.exec();
    };
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        if (!writeValue(it.key(), it.value())) {
            m_database.rollback();
            result.error = query.lastError().text();
            return result;
        }
    }

    const int nextRevision = currentRevision + 1;
    if (!writeValue(QStringLiteral("settings_revision"), QString::number(nextRevision))) {
        m_database.rollback();
        result.error = query.lastError().text();
        return result;
    }
    if (!m_database.commit()) {
        result.error = m_database.lastError().text();
        m_database.rollback();
        return result;
    }
    result.success = true;
    result.revision = nextRevision;
    return result;
}
