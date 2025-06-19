#include "VideoToAsciiWidget.h"
#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication a(argc, argv);

    // 设置OpenCV优化
    cv::setUseOptimized(true);
	// 设置OpenCV线程数
    cv::setNumThreads(QThread::idealThreadCount());

    VideoToAsciiWidget w;
    w.setWindowTitle("Text Animation Converter");
    w.resize(1000, 700);
    w.show();

    return a.exec();
}