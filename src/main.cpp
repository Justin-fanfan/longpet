#include <QApplication>
#include <QFile>
#include <QIODevice>

#include "mainwindow.h"
#include "widgets/VisualTokens.h"

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

    MainWindow window;

#ifdef Q_OS_WIN
    window.setFixedSize(LongPetUi::Metrics::CanvasWidth,
                        LongPetUi::Metrics::CanvasHeight);
    window.show();
#else
    window.showFullScreen();
#endif

    return app.exec();
}
