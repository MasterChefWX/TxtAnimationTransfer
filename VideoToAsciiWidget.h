#ifndef VIDEOTOASCIIWIDGET_H
#define VIDEOTOASCIIWIDGET_H

#include <QWidget>
#include <QProcess>
#include <QDir>
#include <QPlainTextEdit> // 更高效的文本显示控件
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
#include <opencv2/opencv.hpp>
#include <qslider.h>

QT_BEGIN_NAMESPACE
class QPushButton;
class QLabel;
class QProgressBar;
class QTimer;
class QCheckBox;
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
    void adjustFrameDelay(int value);
    void togglePlayback();

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
    QPlainTextEdit* asciiDisplay; // 替换为更高效的文本控件
    QCheckBox* reduceResolutionCheck;
    QSlider* frameDelaySlider;
    QLabel* fpsLabel;

    QProcess ffmpegProcess;
    QString videoPath;
    QDir tempDir;
    QDir asciiDir;
    QStringList asciiFrameFiles; // 存储帧文件路径而不是内容
    int currentFrame = 0;
    QTimer* playTimer;
    int totalFrames = 0;
    QFutureWatcher<void> conversionWatcher;
    bool conversionRunning = false;
    int frameDelay = 33; // 默认30fps
    bool isPlaying = false;
    int skippedFrames = 0;
    int skipFactor = 0;
};
#endif