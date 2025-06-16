#include "VideoToAsciiWidget.h"
#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication a(argc, argv);

    // ����OpenCV�Ż�
    cv::setUseOptimized(true);
    cv::setNumThreads(QThread::idealThreadCount());

    VideoToAsciiWidget w;
    w.setWindowTitle("Text Animation Converter");
    w.resize(1000, 700);
    w.show();

    return a.exec();
}