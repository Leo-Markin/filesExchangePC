#include "filesexchange.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    FileseXchange w;
    w.show();
    return a.exec();
}
