#ifndef VIDEOTOASCIIWIDGET_H
#define VIDEOTOASCIIWIDGET_H

#include <QWidget>
#include <QProcess>
#include <QDir>
#include <QTextBrowser>
#include <QPushButton>  
#include <QLabel>  
#include <QProgressBar>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
#include <opencv2/opencv.hpp>

QT_BEGIN_NAMESPACE
class QPushButton;
class QLabel;
class QProgressBar;
class QTimer;
QT_END_NAMESPACE

class VideoToAsciiWidget : public QWidget {
    Q_OBJECT
public:
    explicit VideoToAsciiWidget(QWidget* parent = nullptr);
    ~VideoToAsciiWidget();

private slots:
    void browseVideo();
    void startConversion();
    void updateProgress(int value);
    void showNextFrame();
    void processFinished(int exitCode);
    void conversionCompleted();

private:
    void setupUI();
    void convertWithFFmpeg();
    void generateAsciiFrames();
    QString imageToAscii(const cv::Mat& image);

    QPushButton* browseBtn;
    QPushButton* convertBtn;
    QPushButton* playBtn;
    QLabel* videoPathLabel;
    QProgressBar* progressBar;
    QTextBrowser* asciiDisplay;

    QProcess ffmpegProcess;
    QString videoPath;
    QDir tempDir;
    QDir asciiDir;
    QStringList asciiFrames;
    int currentFrame = 0;
    QTimer* playTimer;
    int totalFrames = 0;
    QFutureWatcher<void> conversionWatcher;
    bool conversionRunning = false;
};
#endif