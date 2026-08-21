#include "DatabaseManager.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace {
constexpr int CurrentSchemaVersion = 2;

bool execute(QSqlQuery& query, const QString& sql, QString* error)
{
    if (query.exec(sql))
        return true;
    if (error)
        *error = query.lastError().text();
    return false;
}
}

DatabaseManager::DatabaseManager()
    : m_connectionName(QStringLiteral("longpet-%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
}

DatabaseManager::~DatabaseManager()
{
    close();
}

bool DatabaseManager::open(const QString& databasePath, QString* error)
{
    close();

    if (databasePath != QStringLiteral(":memory:")) {
        const QFileInfo info(databasePath);
        if (!info.absoluteDir().mkpath(QStringLiteral("."))) {
            if (error)
                *error = QStringLiteral("无法创建数据目录：%1").arg(info.absolutePath());
            return false;
        }
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(databasePath);
    if (!db.open()) {
        if (error)
            *error = db.lastError().text();
        db = {};
        QSqlDatabase::removeDatabase(m_connectionName);
        return false;
    }
    m_databasePath = databasePath;

    QSqlQuery pragma(db);
    if (!execute(pragma, QStringLiteral("PRAGMA foreign_keys = ON"), error)
        || !execute(pragma, QStringLiteral("PRAGMA busy_timeout = 3000"), error)
        || !migrate(error)) {
        pragma = QSqlQuery();
        db = QSqlDatabase();
        close();
        return false;
    }
    return true;
}

void DatabaseManager::close()
{
    if (!QSqlDatabase::contains(m_connectionName)) {
        m_databasePath.clear();
        return;
    }
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
        if (db.isOpen())
            db.close();
    }
    QSqlDatabase::removeDatabase(m_connectionName);
    m_databasePath.clear();
}

bool DatabaseManager::isOpen() const
{
    return QSqlDatabase::contains(m_connectionName)
        && QSqlDatabase::database(m_connectionName, false).isOpen();
}

QSqlDatabase DatabaseManager::database() const
{
    return QSqlDatabase::database(m_connectionName, false);
}

QString DatabaseManager::databasePath() const
{
    return m_databasePath;
}

int DatabaseManager::schemaVersion() const
{
    if (!isOpen())
        return 0;
    QSqlQuery query(database());
    if (!query.exec(QStringLiteral("SELECT version FROM schema_meta LIMIT 1"))
        || !query.next()) {
        return 0;
    }
    return query.value(0).toInt();
}

bool DatabaseManager::migrate(QString* error)
{
    QSqlQuery query(database());
    if (!execute(query, QStringLiteral(
            "CREATE TABLE IF NOT EXISTS schema_meta (version INTEGER NOT NULL)"), error)) {
        return false;
    }

    if (!query.exec(QStringLiteral("SELECT version FROM schema_meta LIMIT 1"))) {
        if (error)
            *error = query.lastError().text();
        return false;
    }

    int version = 0;
    if (query.next()) {
        version = query.value(0).toInt();
    } else if (!execute(query, QStringLiteral("INSERT INTO schema_meta(version) VALUES (0)"), error)) {
        return false;
    }

    if (version > CurrentSchemaVersion) {
        if (error)
            *error = QStringLiteral("数据库版本 %1 高于程序支持版本 %2")
                         .arg(version).arg(CurrentSchemaVersion);
        return false;
    }
    if (version == 0) {
        if (!createSchemaVersion1(error))
            return false;
        version = 1;
    }
    if (version == 1)
        return migrateVersion1To2(error);
    return true;
}

bool DatabaseManager::createSchemaVersion1(QString* error)
{
    QSqlDatabase db = database();
    if (!db.transaction()) {
        if (error)
            *error = db.lastError().text();
        return false;
    }

    QSqlQuery query(db);
    const QStringList statements {
        QStringLiteral(
            "CREATE TABLE reminders ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, type TEXT NOT NULL, title TEXT NOT NULL, "
            "time_of_day TEXT NOT NULL, scheduled_date TEXT, repeat_rule TEXT NOT NULL, enabled INTEGER NOT NULL, "
            "revision INTEGER NOT NULL DEFAULT 1, created_at TEXT NOT NULL, updated_at TEXT NOT NULL)"),
        QStringLiteral(
            "CREATE TABLE reminder_events ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, reminder_id INTEGER NOT NULL, "
            "scheduled_at TEXT NOT NULL, status TEXT NOT NULL, completed_at TEXT, "
            "FOREIGN KEY(reminder_id) REFERENCES reminders(id) ON DELETE CASCADE)"),
        QStringLiteral(
            "CREATE TABLE care_events ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, event_type TEXT NOT NULL, value INTEGER NOT NULL DEFAULT 1, "
            "source TEXT NOT NULL, created_at TEXT NOT NULL)"),
        QStringLiteral(
            "CREATE TABLE settings (key TEXT PRIMARY KEY, value TEXT NOT NULL, updated_at TEXT NOT NULL)"),
        QStringLiteral("CREATE INDEX idx_reminders_enabled_time ON reminders(enabled, time_of_day)"),
        QStringLiteral("CREATE INDEX idx_reminder_events_scheduled ON reminder_events(scheduled_at)"),
        QStringLiteral("CREATE INDEX idx_care_events_created ON care_events(created_at)"),
        QStringLiteral("UPDATE schema_meta SET version = 1")
    };

    for (const QString& sql : statements) {
        if (!execute(query, sql, error)) {
            db.rollback();
            return false;
        }
    }
    if (!db.commit()) {
        if (error)
            *error = db.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::migrateVersion1To2(QString* error)
{
    QSqlDatabase db = database();
    if (!db.transaction()) {
        if (error)
            *error = db.lastError().text();
        return false;
    }

    QSqlQuery query(db);
    const QStringList alterStatements {
        QStringLiteral("ALTER TABLE reminders ADD COLUMN uuid TEXT"),
        QStringLiteral("ALTER TABLE reminders ADD COLUMN icon_key TEXT"),
        QStringLiteral("ALTER TABLE reminders ADD COLUMN voice_type TEXT NOT NULL DEFAULT 'none'"),
        QStringLiteral("ALTER TABLE reminders ADD COLUMN voice_text TEXT NOT NULL DEFAULT ''"),
        QStringLiteral("ALTER TABLE reminders ADD COLUMN voice_asset_id TEXT NOT NULL DEFAULT ''"),
        QStringLiteral("ALTER TABLE reminders ADD COLUMN repeat_interval_minutes INTEGER NOT NULL DEFAULT 5"),
        QStringLiteral("ALTER TABLE reminders ADD COLUMN max_repeat_count INTEGER NOT NULL DEFAULT 3"),
        QStringLiteral("ALTER TABLE reminder_events ADD COLUMN presentation_count INTEGER NOT NULL DEFAULT 0"),
        QStringLiteral("ALTER TABLE reminder_events ADD COLUMN last_presented_at TEXT"),
        QStringLiteral("ALTER TABLE reminder_events ADD COLUMN acknowledged_at TEXT"),
        QStringLiteral("ALTER TABLE reminder_events ADD COLUMN ack_source TEXT NOT NULL DEFAULT 'none'")
    };
    for (const QString& sql : alterStatements) {
        if (!execute(query, sql, error)) {
            db.rollback();
            return false;
        }
    }

    struct LegacyReminder {
        qint64 id = 0;
        QString type;
        QString title;
    };
    QList<LegacyReminder> legacyReminders;
    if (!query.exec(QStringLiteral("SELECT id,type,title FROM reminders"))) {
        if (error)
            *error = query.lastError().text();
        db.rollback();
        return false;
    }
    while (query.next()) {
        legacyReminders.append({query.value(0).toLongLong(),
                                query.value(1).toString(),
                                query.value(2).toString()});
    }

    for (const LegacyReminder& reminder : legacyReminders) {
        const QString iconKey = reminder.type == QStringLiteral("medicine")
            ? QStringLiteral("medicine")
            : reminder.type == QStringLiteral("water")
                ? QStringLiteral("water") : QStringLiteral("reminder");
        query.prepare(QStringLiteral(
            "UPDATE reminders SET uuid=?,icon_key=?,voice_text=? WHERE id=?"));
        query.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
        query.addBindValue(iconKey);
        query.addBindValue(reminder.title);
        query.addBindValue(reminder.id);
        if (!query.exec()) {
            if (error)
                *error = query.lastError().text();
            db.rollback();
            return false;
        }
    }

    const QStringList finishStatements {
        QStringLiteral("CREATE UNIQUE INDEX idx_reminders_uuid ON reminders(uuid)"),
        QStringLiteral("CREATE INDEX idx_reminder_events_status ON reminder_events(status, last_presented_at)"),
        QStringLiteral("UPDATE schema_meta SET version = 2")
    };
    for (const QString& sql : finishStatements) {
        if (!execute(query, sql, error)) {
            db.rollback();
            return false;
        }
    }
    if (!db.commit()) {
        if (error)
            *error = db.lastError().text();
        return false;
    }
    return true;
}
