#include <QApplication>
#include <QtGlobal>

#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    MainWindow window;

#ifdef Q_OS_WIN

    // Windows 开发预览模式
    window.setFixedSize(1024, 600);
    window.show();

#else

    // 2K0300 产品模式
    window.showFullScreen();

#endif

    return app.exec();
}