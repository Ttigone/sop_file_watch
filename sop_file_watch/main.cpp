#include "stdafx.h"
#include "sop_file_watch.h"
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    SopFileWatch window;
    window.show();
    return app.exec();
}
