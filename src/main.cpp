#include <QApplication>
#include <QLabel>
#include <uiohook.h>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QLabel l("Typing statistics — toolchain OK");
    l.show();
    return app.exec();
}
