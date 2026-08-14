#pragma once

#include <QHash>
#include <QSqlDatabase>
#include <QString>

class SettingsRepository final {
public:
    explicit SettingsRepository(const QSqlDatabase& database);

    QHash<QString, QString> all(QString* error = nullptr) const;
    bool setValue(const QString& key, const QString& value, QString* error = nullptr);

private:
    QSqlDatabase m_database;
};

