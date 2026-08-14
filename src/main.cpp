#include <QApplication>
#include <QFile>
#include <QIODevice>
#include <QMessageBox>

#include "app/Application.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("LongPet"));
    QApplication::setApplicationVersion(QStringLiteral(LONGPET_VERSION));
    QApplication::setOrganizationName(QStringLiteral("LongPet"));

    QFile styleFile(QStringLiteral(":/styles/app.qss"));
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        app.setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    } else {
        qWarning("LongPet: failed to load the embedded application stylesheet");
    }

    Application longPet;
    QString error;
    if (!longPet.initialize(&error)) {
        qCritical("LongPet initialization failed: %s", qPrintable(error));
        QMessageBox::critical(nullptr, QStringLiteral("LongPet 无法启动"),
                              QStringLiteral("本地数据初始化失败：\n%1").arg(error));
        return 1;
    }
    longPet.show();
    const int exitCode = app.exec();
    longPet.shutdown();
    return exitCode;
}
