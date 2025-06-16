#include "VideoToAsciiWidget.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QApplication>
#include <opencv2/opencv.hpp>

VideoToAsciiWidget::VideoToAsciiWidget(QWidget* parent) : QWidget(parent) {
    tempDir = QDir("Temp");
    asciiDir = QDir("Ascii");
    if (!tempDir.exists()) tempDir.mkpath(".");
    if (!asciiDir.exists()) asciiDir.mkpath(".");

    setupUI();
    playTimer = new QTimer(this);
    connect(playTimer, &QTimer::timeout, this, &VideoToAsciiWidget::showNextFrame);
}

VideoToAsciiWidget::~VideoToAsciiWidget() {
    if (ffmpegProcess.state() == QProcess::Running) {
        ffmpegProcess.terminate();
        ffmpegProcess.waitForFinished();
    }
}

void VideoToAsciiWidget::setupUI() {
    QVBoxLayout* layout = new QVBoxLayout;

    browseBtn = new QPushButton("Browse vedios");
    convertBtn = new QPushButton("Strat to Transfer");
    playBtn = new QPushButton("Display the Animation");
    videoPathLabel = new QLabel("Have not select the vedio!");
    progressBar = new QProgressBar;
    asciiDisplay = new QTextBrowser;
    asciiDisplay->setFont(QFont("Courier", 8));

    connect(browseBtn, &QPushButton::clicked, this, &VideoToAsciiWidget::browseVideo);
    connect(convertBtn, &QPushButton::clicked, this, &VideoToAsciiWidget::startConversion);
    connect(playBtn, &QPushButton::clicked, [this] {
        currentFrame = 0;
        playTimer->start(5);
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
    videoPath = QFileDialog::getOpenFileName(this, "Select the Vedio", "", "Vedio(*.mp4 *.avi *.mov)");
    videoPathLabel->setText(videoPath.isEmpty() ? "Have not select the vedio!" : QFileInfo(videoPath).fileName());
}

void VideoToAsciiWidget::startConversion() {
    if (videoPath.isEmpty()) {
        QMessageBox::warning(this, "error", "please select the vedio");
        return;
    }
    convertBtn->setEnabled(false); // 禁用按钮避免重复点击
    playBtn->setEnabled(false);
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
    connect(&ffmpegProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        [this](int exitCode, QProcess::ExitStatus) {
            processFinished(exitCode);
        });
}

void VideoToAsciiWidget::processFinished(int exitCode) {
    if (exitCode == 0) {
        generateAsciiFrames();
    }
    else {
        QMessageBox::critical(this, "error", "faild");
        convertBtn->setEnabled(true); // 重新启用按钮
        playBtn->setEnabled(true);
    }
}

void VideoToAsciiWidget::generateAsciiFrames() {
    asciiDir.removeRecursively();
    asciiDir.mkpath(".");
    asciiFrames.clear();

    // 按名称排序确保帧顺序正确
    QStringList images = tempDir.entryList({ "*.jpg" }, QDir::Files, QDir::Name);
    const int total = images.count();

    for (int i = 0; i < images.count(); ++i) {
        // 使用本地编码处理路径
        QString imagePath = tempDir.filePath(images[i]);
        cv::Mat image = cv::imread(imagePath.toLocal8Bit().constData(), cv::IMREAD_GRAYSCALE);
        if (!image.empty()) {
            QString ascii = imageToAscii(image);
            QFile file(asciiDir.filePath(QString("frame_%1.txt").arg(i + 1, 4, 10, QChar('0'))));
            if (file.open(QIODevice::WriteOnly)) {
                file.write(ascii.toUtf8());
                asciiFrames << ascii;
            }
        }
        updateProgress(i + 1, total);
        QApplication::processEvents(); // 处理事件避免界面冻结
    }
    QMessageBox::information(this, "completed", "success");
    convertBtn->setEnabled(true);
    playBtn->setEnabled(true);
}

QString VideoToAsciiWidget::imageToAscii(const cv::Mat& image) {
    const char asciiChars[] = "@%#*+=-:. ";
    const int asciiCharsCount = sizeof(asciiChars) - 1; // 排除终止符
    cv::Mat resized;
    cv::resize(image, resized, cv::Size(100, 50));

    QString result;
    for (int y = 0; y < resized.rows; ++y) {
        for (int x = 0; x < resized.cols; ++x) {
            uchar pixel = resized.at<uchar>(y, x);
            // 修正索引计算防止越界
            int index = (pixel * asciiCharsCount) / 256;
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