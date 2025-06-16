#include "VideoToAsciiWidget.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QApplication>
#include <QDebug>
#include <opencv2/opencv.hpp>

VideoToAsciiWidget::VideoToAsciiWidget(QWidget* parent) : QWidget(parent) {
    // 使用唯一临时目录名避免冲突
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    tempDir = QDir("Temp_" + timestamp);
    asciiDir = QDir("Ascii_" + timestamp);

    if (!tempDir.exists()) tempDir.mkpath(".");
    if (!asciiDir.exists()) asciiDir.mkpath(".");

    setupUI();
    playTimer = new QTimer(this);
    playTimer->setTimerType(Qt::PreciseTimer);
    connect(playTimer, &QTimer::timeout, this, &VideoToAsciiWidget::showNextFrame);
    connect(&conversionWatcher, &QFutureWatcher<void>::finished, this, &VideoToAsciiWidget::conversionCompleted);
}

VideoToAsciiWidget::~VideoToAsciiWidget() {
    // 停止所有后台任务
    if (conversionRunning) {
        conversionWatcher.cancel();
        conversionWatcher.waitForFinished();
    }

    if (ffmpegProcess.state() == QProcess::Running) {
        ffmpegProcess.terminate();
        ffmpegProcess.waitForFinished();
    }

    // 清理资源
    playTimer->stop();
    delete playTimer;

    // 删除临时目录
    if (tempDir.exists()) tempDir.removeRecursively();
    if (asciiDir.exists()) asciiDir.removeRecursively();
}

void VideoToAsciiWidget::setupUI() {
    QVBoxLayout* layout = new QVBoxLayout;

    browseBtn = new QPushButton("Browse Video");
    convertBtn = new QPushButton("Start Conversion");
    playBtn = new QPushButton("Play Animation");
    videoPathLabel = new QLabel("No video selected!");
    progressBar = new QProgressBar;
    progressBar->setRange(0, 100);
    asciiDisplay = new QTextBrowser;
    asciiDisplay->setFont(QFont("Courier", 8));
    asciiDisplay->setMinimumHeight(300);

    connect(browseBtn, &QPushButton::clicked, this, &VideoToAsciiWidget::browseVideo);
    connect(convertBtn, &QPushButton::clicked, this, &VideoToAsciiWidget::startConversion);
    connect(playBtn, &QPushButton::clicked, [this] {
        currentFrame = 0;
        playTimer->start(33); // ~30 fps
        });

    // 禁用播放按钮直到转换完成
    playBtn->setEnabled(false);

    QHBoxLayout* btnLayout = new QHBoxLayout;
    btnLayout->addWidget(browseBtn);
    btnLayout->addWidget(convertBtn);
    btnLayout->addWidget(playBtn);

    layout->addLayout(btnLayout);
    layout->addWidget(videoPathLabel);
    layout->addWidget(progressBar);
    layout->addWidget(asciiDisplay, 1);
    setLayout(layout);
}

void VideoToAsciiWidget::browseVideo() {
    videoPath = QFileDialog::getOpenFileName(this, "Select Video", "",
        "Video Files (*.mp4 *.avi *.mov *.mkv)");
    videoPathLabel->setText(videoPath.isEmpty() ? "No video selected!" : QFileInfo(videoPath).fileName());
    playBtn->setEnabled(false);
}

void VideoToAsciiWidget::startConversion() {
    if (videoPath.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select a video file");
        return;
    }

    if (conversionRunning) {
        QMessageBox::warning(this, "Error", "Conversion already in progress");
        return;
    }

    convertBtn->setEnabled(false);
    playBtn->setEnabled(false);
    progressBar->setValue(0);
    asciiFrames.clear();
    conversionRunning = true;

    convertWithFFmpeg();
}

void VideoToAsciiWidget::convertWithFFmpeg() {
    // 清理旧文件
    if (tempDir.exists()) tempDir.removeRecursively();
    tempDir.mkpath(".");

    QStringList args;
    args << "-y" // 覆盖输出文件
        << "-i" << videoPath
        << "-vf" << "fps=30,scale=200:100" // 提前缩小尺寸减少处理量
        << "-q:v" << "2" // 控制JPEG质量
        << tempDir.filePath("frame_%05d.jpg");

    ffmpegProcess.start("ffmpeg", args);
    connect(&ffmpegProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        this, &VideoToAsciiWidget::processFinished);
}

void VideoToAsciiWidget::processFinished(int exitCode) {
    if (exitCode == 0) {
        // 使用QtConcurrent在后台线程处理
        QFuture<void> future = QtConcurrent::run([this]() {
            generateAsciiFrames();
            });
        conversionWatcher.setFuture(future);
    }
    else {
        QMessageBox::critical(this, "Error", "FFmpeg processing failed: " + ffmpegProcess.errorString());
        convertBtn->setEnabled(true);
        conversionRunning = false;
    }
}

void VideoToAsciiWidget::generateAsciiFrames() {
    if (asciiDir.exists()) asciiDir.removeRecursively();
    asciiDir.mkpath(".");

    QStringList images = tempDir.entryList({ "*.jpg" }, QDir::Files, QDir::Name);
    totalFrames = images.size();
    if (totalFrames == 0) return;

    const char asciiChars[] = "@%#*+=-:. ";
    const int asciiCharsCount = sizeof(asciiChars) - 1;
    int processedFrames = 0;

    foreach(const QString & imageName, images) {
        if (conversionWatcher.isCanceled()) break;

        QString imagePath = tempDir.filePath(imageName);
        cv::Mat image = cv::imread(imagePath.toStdString(), cv::IMREAD_GRAYSCALE);

        if (!image.empty()) {
            QString ascii;
            ascii.reserve((image.cols + 1) * image.rows); // 预分配内存

            for (int y = 0; y < image.rows; ++y) {
                for (int x = 0; x < image.cols; ++x) {
                    uchar pixel = image.at<uchar>(y, x);
                    int index = (pixel * asciiCharsCount) / 256;
                    ascii += asciiChars[index];
                }
                ascii += '\n';
            }

            asciiFrames.append(ascii);
            QFile file(asciiDir.filePath(imageName + ".txt"));
            if (file.open(QIODevice::WriteOnly)) {
                file.write(ascii.toUtf8());
            }
        }

        processedFrames++;
        int progress = (processedFrames * 100) / totalFrames;
        QMetaObject::invokeMethod(this, "updateProgress", Qt::QueuedConnection, Q_ARG(int, progress));
    }
}

QString VideoToAsciiWidget::imageToAscii(const cv::Mat& image) {
    // 已集成到generateAsciiFrames中优化性能
    return QString();
}

void VideoToAsciiWidget::updateProgress(int value) {
    progressBar->setValue(value);
}

void VideoToAsciiWidget::conversionCompleted() {
    conversionRunning = false;
    convertBtn->setEnabled(true);
    playBtn->setEnabled(!asciiFrames.isEmpty());

    if (!conversionWatcher.isCanceled() && !asciiFrames.isEmpty()) {
        QMessageBox::information(this, "Completed", "Conversion successful!");
    }
    else {
        QMessageBox::warning(this, "Cancelled", "Conversion was cancelled");
    }
}

void VideoToAsciiWidget::showNextFrame() {
    if (asciiFrames.isEmpty()) {
        playTimer->stop();
        return;
    }

    if (currentFrame >= asciiFrames.size()) {
        currentFrame = 0;
    }

    asciiDisplay->setPlainText(asciiFrames[currentFrame]);
    currentFrame++;
}