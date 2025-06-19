#include "VideoToAsciiWidget.h"
#include <QApplication>
#include <QStyleFactory>

int main(int argc, char* argv[]) {
    QApplication a(argc, argv);

    // ����Ӧ�ó�����ʽ
    QApplication::setStyle(QStyleFactory::create("Fusion"));

    // ���õ�ɫ�壬�������н���ui����
    //��53��53��53��������ý�Ϊ��͵ĺڻ�ɫ
	//��25��25��25���������ý���ĺ�ɫ����
	//��42��130��218�����������ð�һ�����ɫ
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

    // ����OpenCV�Ż�
    cv::setUseOptimized(true);
    cv::setNumThreads(QThread::idealThreadCount());

    VideoToAsciiWidget w;
    w.setWindowTitle("Text Animation Converter - Optimized");
    w.resize(1000, 700);
    w.show();

    return a.exec();
}