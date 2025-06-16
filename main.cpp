#include "VideoToAsciiWidget.h"
#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication a(argc, argv);
    VideoToAsciiWidget w;
    w.setWindowTitle("ÊÓÆµ×ª×Ö·û¶¯»­");
    w.resize(800, 600);
    w.show();
    return a.exec();
}
