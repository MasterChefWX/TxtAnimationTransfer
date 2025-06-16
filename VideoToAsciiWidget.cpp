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
    // ʹ��Ψһ��ʱĿ¼�������ͻ
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
    // ֹͣ���к�̨����
    if (conversionRunning) {
        conversionWatcher.cancel();
        conversionWatcher.waitForFinished();
    }

    if (ffmpegProcess.state() == QProcess::Running) {
        ffmpegProcess.terminate();
        ffmpegProcess.waitForFinished();
    }

    // ������Դ
    playTimer->stop();
    delete playTimer;

    // ɾ����ʱĿ¼
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

    // ���ò��Ű�ťֱ��ת�����
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
    // ������ļ�
    if (tempDir.exists()) tempDir.removeRecursively();
    tempDir.mkpath(".");

    QStringList args;
    args << "-y" // ��������ļ�
        << "-i" << videoPath
        << "-vf" << "fps=30,scale=200:100" // ��ǰ��С�ߴ���ٴ�����
        << "-q:v" << "2" // ����JPEG����
        << tempDir.filePath("frame_%05d.jpg");

    ffmpegProcess.start("ffmpeg", args);
    connect(&ffmpegProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        this, &VideoToAsciiWidget::processFinished);
}

void VideoToAsciiWidget::processFinished(int exitCode) {
    if (exitCode == 0) {
        // ʹ��QtConcurrent�ں�̨�̴߳���
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
            ascii.reserve((image.cols + 1) * image.rows); // Ԥ�����ڴ�

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
    // �Ѽ��ɵ�generateAsciiFrames���Ż�����
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