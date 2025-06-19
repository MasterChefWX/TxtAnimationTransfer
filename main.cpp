#include "VideoToAsciiWidget.h"
#include <QApplication>
#include <QStyleFactory>

int main(int argc, char* argv[]) {
    QApplication a(argc, argv);

    // 设置应用程序样式
    QApplication::setStyle(QStyleFactory::create("Fusion"));

    // 设置调色板，后续进行界面ui调整
    //（53，53，53）界面采用较为柔和的黑灰色
	//（25，25，25）背景采用较深的黑色背景
	//（42，130，218）进度条采用暗一点的蓝色
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(53, 53, 53));
    palette.setColor(QPalette::WindowText, Qt::white);
    palette.setColor(QPalette::Base, QColor(25, 25, 25));
    palette.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
    palette.setColor(QPalette::ToolTipBase, Qt::white);
    palette.setColor(QPalette::ToolTipText, Qt::white);
    palette.setColor(QPalette::Text, Qt::white);
    palette.setColor(QPalette::Button, QColor(53, 53, 53));
    palette.setColor(QPalette::ButtonText, Qt::white);
    palette.setColor(QPalette::BrightText, Qt::red);
    palette.setColor(QPalette::Link, QColor(42, 130, 218));
    palette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    palette.setColor(QPalette::HighlightedText, Qt::black);
    a.setPalette(palette);

    // 设置OpenCV优化
    cv::setUseOptimized(true);
    cv::setNumThreads(QThread::idealThreadCount());

    VideoToAsciiWidget w;
    w.setWindowTitle("Text Animation Converter - Optimized");
    w.resize(1000, 700);
    w.show();

    return a.exec();
}