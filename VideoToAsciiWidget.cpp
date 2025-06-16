#include"VideoToAsciiWidget.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <opencv2/opencv.hpp>

VideoToAsciiWidget::VideoToAsciiWidget(QWidget * parent) : QWidget(parent) {
    tempDir = QDir("Temp");
    asciiDir = QDir("Ascii");
    if (!tempDir.exists()) tempDir.mkpath(".");
    if (!asciiDir.exists()) asciiDir.mkpath(".");

    setupUI();
    playTimer = new QTimer(this);
    connect(playTimer, &QTimer::timeout, this, &VideoToAsciiWidget::showNextFrame);
}

void VideoToAsciiWidget::setupUI() {
    // UI组件初始化...
    QVBoxLayout* layout = new QVBoxLayout;

    browseBtn = new QPushButton("浏览视频");
    convertBtn = new QPushButton("开始转换");
    playBtn = new QPushButton("播放动画");
    videoPathLabel = new QLabel("未选择视频");
    progressBar = new QProgressBar;
    asciiDisplay = new QTextBrowser;
    asciiDisplay->setFont(QFont("Courier", 8));

    connect(browseBtn, &QPushButton::clicked, this, &VideoToAsciiWidget::browseVideo);
    connect(convertBtn, &QPushButton::clicked, this, &VideoToAsciiWidget::startConversion);
    connect(playBtn, &QPushButton::clicked, [this] {
        currentFrame = 0;
        playTimer->start(100);
        });

    layout->addWidget(browseBtn);
    layout->addWidget(videoPathLabel);
    layout->addWidget(progressBar);
    layout->addWidget(convertBtn);
    layout->addWidget(playBtn);
    layout->addWidget(asciiDisplay);
    setLayout(layout);
}

void VideoToAsciiWidget::browseVideo() {
    videoPath = QFileDialog::getOpenFileName(this, "选择视频文件", "", "视频文件 (*.mp4 *.avi *.mov)");
    videoPathLabel->setText(videoPath.isEmpty() ? "未选择视频" : QFileInfo(videoPath).fileName());
}

void VideoToAsciiWidget::startConversion() {
    if (videoPath.isEmpty()) {
        QMessageBox::warning(this, "错误", "请先选择视频文件");
        return;
    }
    convertWithFFmpeg();
}

void VideoToAsciiWidget::convertWithFFmpeg() {
    tempDir.removeRecursively();
    tempDir.mkpath(".");

    QStringList args;
    args << "-i" << videoPath
        << "-vf" << "fps=30"
        << tempDir.filePath("frame_%05d.jpg");

    ffmpegProcess.start("ffmpeg", args);
    connect(&ffmpegProcess, QOverload<int>::of(&QProcess::finished),
        this, &VideoToAsciiWidget::processFinished);
}

void VideoToAsciiWidget::processFinished(int exitCode) {
    if (exitCode == 0) {
        generateAsciiFrames();
    }
    else {
        QMessageBox::critical(this, "错误", "视频转换失败");
    }
}

void VideoToAsciiWidget::generateAsciiFrames() {
    asciiDir.removeRecursively();
    asciiDir.mkpath(".");
    asciiFrames.clear();

    QStringList images = tempDir.entryList({ "*.jpg" }, QDir::Files);
    const int total = images.count();

    for (int i = 0; i < images.count(); ++i) {
        cv::Mat image = cv::imread(tempDir.filePath(images[i]).toStdString(), cv::IMREAD_GRAYSCALE);
        if (!image.empty()) {
            QString ascii = imageToAscii(image);
            QFile file(asciiDir.filePath(QString("frame_%1.txt").arg(i + 1, 4, 10, QChar('0'))));
            if (file.open(QIODevice::WriteOnly)) {
                file.write(ascii.toUtf8());
                asciiFrames << ascii;
            }
        }
        updateProgress(i + 1, total);
    }
    QMessageBox::information(this, "完成", "转换完成！");
}

QString VideoToAsciiWidget::imageToAscii(const cv::Mat& image) {
    const char asciiChars[] = "@%#*+=-:. ";
    cv::Mat resized;
    cv::resize(image, resized, cv::Size(100, 50));

    QString result;
    for (int y = 0; y < resized.rows; ++y) {
        for (int x = 0; x < resized.cols; ++x) {
            int index = resized.at<uchar>(y, x) * (sizeof(asciiChars) - 1) / 255;
            result += asciiChars[index];
        }
        result += "\n";
    }
    return result;
}

void VideoToAsciiWidget::updateProgress(int frame, int total) {
    progressBar->setMaximum(total);
    progressBar->setValue(frame);
}

void VideoToAsciiWidget::showNextFrame() {
    if (currentFrame < asciiFrames.count()) {
        asciiDisplay->setPlainText(asciiFrames[currentFrame++]);
    }
    else {
        playTimer->stop();
    }
}
