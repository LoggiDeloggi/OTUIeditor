#include "corewindow.h"
#include "startupwindow.h"
#include <QApplication>
#include <QFile>
#include <QSurfaceFormat>
#include <QMessageBox>
#include <QDebug>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QFile File(":/stylesheet.css");
    if(File.open(QFile::ReadOnly))
        a.setStyleSheet(File.readAll());
    else
        qWarning() << "Failed to load stylesheet:" << File.errorString();

    StartupWindow w;
    w.show();

    return a.exec();
}
