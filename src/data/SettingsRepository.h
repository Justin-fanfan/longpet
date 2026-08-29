#pragma once

#include <QHash>
#include <QSqlDatabase>
#include <QString>

struct SettingsRepositoryWriteResult {
    bool success = false;
    bool revisionConflict = false;
    int revision = 0;
    QString error;
};

class SettingsRepository final {
public:
    explicit SettingsRepository(const QSqlDatabase& database);

    QHash<QString, QString> all(QString* error = nullptr) const;
    int revision(QString* error = nullptr) const;
    bool setValue(const QString& key, const QString& value, QString* error = nullptr);
    SettingsRepositoryWriteResult setValues(const QHash<QString, QString>& values,
                                            int expectedRevision = -1);

private:
    QSqlDatabase m_database;
};
