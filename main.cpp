#include "VideoToAsciiWidget.h"
#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication a(argc, argv);
    VideoToAsciiWidget w;
    w.setWindowTitle("��Ƶת�ַ�����");
    w.resize(800, 600);
    w.show();
    return a.exec();
}
