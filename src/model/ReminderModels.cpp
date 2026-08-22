#include "ReminderModels.h"

QString defaultReminderIconKey(ReminderType type)
{
    switch (type) {
    case ReminderType::Medicine: return QStringLiteral("medicine");
    case ReminderType::Water: return QStringLiteral("water");
    case ReminderType::Other: return QStringLiteral("reminder");
    }
    return QStringLiteral("reminder");
}

QString reminderIconResourcePath(const QString& iconKey)
{
    const QString key = iconKey.trimmed().toLower();
    if (key == QStringLiteral("medicine") || key == QStringLiteral("pill"))
        return QStringLiteral(":/icons/pill.svg");
    if (key == QStringLiteral("water"))
        return QStringLiteral(":/icons/water.svg");
    if (key == QStringLiteral("activity"))
        return QStringLiteral(":/icons/activity.svg");
    if (key == QStringLiteral("care"))
        return QStringLiteral(":/icons/care.svg");
    return QStringLiteral(":/icons/reminder.svg");
}
