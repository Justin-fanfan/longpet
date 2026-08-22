#pragma once

#include <QSqlDatabase>
#include <QString>

class DatabaseManager final {
public:
    DatabaseManager();
    ~DatabaseManager();

    bool open(const QString& databasePath, QString* error = nullptr);
    void close();
    bool isOpen() const;
    QSqlDatabase database() const;
    QString databasePath() const;
    int schemaVersion() const;

private:
    bool migrate(QString* error);
    bool createSchemaVersion1(QString* error);
    bool migrateVersion1To2(QString* error);

    QString m_connectionName;
    QString m_databasePath;
};
