#include "VideoToAsciiWidget.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QApplication>
#include <QDebug>
#include <QSlider>
#include <QCheckBox>
#include <QGroupBox>
#include <QScrollBar>
#include <opencv2/opencv.hpp>

VideoToAsciiWidget::VideoToAsciiWidget(QWidget* parent) : QWidget(parent) {
    // 生成两个临时目录，一个用于存储FFmpeg生成的图像帧，另一个用于存储ASCII文本文件
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");//生成逻辑：后缀加上当前时间，避免目录名冲突
    tempDir = QDir("Temp_" + timestamp);//定位图像帧目录位置，便于后续调用FFmpeg
    asciiDir = QDir("Ascii_" + timestamp);//定位ASCII文本目录位置，便于后续调用opencv

    // 确保目录存在
    if (!tempDir.exists()) tempDir.mkpath(".");
    if (!asciiDir.exists()) asciiDir.mkpath(".");

    setupUI();
    playTimer = new QTimer(this);// 创建一个定时器用于播放ASCII动画，并控制播放速度
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
    QVBoxLayout* mainLayout = new QVBoxLayout;

    // 创建控制面板（已做出更细致的区域划分）
    QGroupBox* controlGroup = new QGroupBox("Controls");
    QVBoxLayout* controlLayout = new QVBoxLayout;

    QHBoxLayout* fileLayout = new QHBoxLayout;
    browseBtn = new QPushButton("Browse Video");
    videoPathLabel = new QLabel("No video selected!");
    videoPathLabel->setMinimumWidth(300);
    fileLayout->addWidget(browseBtn);
    fileLayout->addWidget(videoPathLabel);
    controlLayout->addLayout(fileLayout);

    QHBoxLayout* buttonLayout = new QHBoxLayout;
    convertBtn = new QPushButton("Start Conversion");
    playBtn = new QPushButton("? Play");
    buttonLayout->addWidget(convertBtn);
    buttonLayout->addWidget(playBtn);
    controlLayout->addLayout(buttonLayout);

    // 分辨率选项，可选择是否降低分辨率以提高播放流畅度
    reduceResolutionCheck = new QCheckBox("Reduce resolution for smoother playback");
	reduceResolutionCheck->setChecked(true); // 默认选中，降低分辨率以提高播放流畅度
    controlLayout->addWidget(reduceResolutionCheck);

    // 帧率控制
    QHBoxLayout* fpsLayout = new QHBoxLayout;
    fpsLabel = new QLabel("Playback Speed: 30 FPS");
    frameDelaySlider = new QSlider(Qt::Horizontal);
    frameDelaySlider->setRange(10, 200); // 控制每一帧的输出时间间隔，从而控制播放速率
    frameDelaySlider->setValue(33); // 30 FPS
    frameDelaySlider->setTickPosition(QSlider::TicksBelow);
    frameDelaySlider->setTickInterval(10);
    fpsLayout->addWidget(new QLabel("Fast"));
    fpsLayout->addWidget(frameDelaySlider);
    fpsLayout->addWidget(new QLabel("Slow"));
    fpsLayout->addWidget(fpsLabel);
    controlLayout->addLayout(fpsLayout);

    controlGroup->setLayout(controlLayout);
    mainLayout->addWidget(controlGroup);

    // 进度条（新增文字信息显示）
    progressBar = new QProgressBar;
    progressBar->setRange(0, 100);
    progressBar->setTextVisible(true);
    progressBar->setFormat("Idle");
    mainLayout->addWidget(progressBar);

    // ASCII显示区域（更新为更高效的QPlainTextEdit控件，支持大文本显示和更快的渲染速度）
    QGroupBox* displayGroup = new QGroupBox("ASCII Animation");//标题
    QVBoxLayout* displayLayout = new QVBoxLayout;

    asciiDisplay = new QPlainTextEdit;
    asciiDisplay->setReadOnly(true);// 设置为只读模式，防止用户编辑内容
    asciiDisplay->setFont(QFont("Courier", 6)); // 使用更小的字体，降低渲染压力
    asciiDisplay->setLineWrapMode(QPlainTextEdit::NoWrap);// 禁用自动换行，防止因窗口大小变化导致的渲染问题
    asciiDisplay->setCenterOnScroll(true); // 设置滚动条策略，确保文本可以滚动

    displayLayout->addWidget(asciiDisplay);
    displayGroup->setLayout(displayLayout);
    mainLayout->addWidget(displayGroup, 1);

    setLayout(mainLayout);

    // 连接信号槽
    connect(browseBtn, &QPushButton::clicked, this, &VideoToAsciiWidget::browseVideo);
    connect(convertBtn, &QPushButton::clicked, this, &VideoToAsciiWidget::startConversion);
    connect(playBtn, &QPushButton::clicked, this, &VideoToAsciiWidget::togglePlayback);
    connect(frameDelaySlider, &QSlider::valueChanged, this, &VideoToAsciiWidget::adjustFrameDelay);
}

// 选择视频文件
void VideoToAsciiWidget::browseVideo() {
    videoPath = QFileDialog::getOpenFileName(this, "Select Video", "",
        "Video Files (*.mp4 *.avi *.mov *.mkv)");
    videoPathLabel->setText(videoPath.isEmpty() ? "No video selected!" : QFileInfo(videoPath).fileName());
    playBtn->setEnabled(false);
    playBtn->setText("? Play");
    isPlaying = false;
}

// 开始转换
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
    progressBar->setFormat("Processing: %p%");
    asciiFrameFiles.clear();
    conversionRunning = true;
    skippedFrames = 0;

    convertWithFFmpeg();
}

//调用FFmpeg进行视频转换
void VideoToAsciiWidget::convertWithFFmpeg() {
    // 清理旧文件
    if (tempDir.exists()) tempDir.removeRecursively();
    tempDir.mkpath(".");

    QStringList args;
    args << "-y" // 覆盖输出文件
        << "-i" << videoPath
        << "-vf" << "fps=30" // 保持30fps
        << "-q:v" << "2" // 控制JPEG质量
        << tempDir.filePath("frame_%05d.jpg");

    ffmpegProcess.start("ffmpeg", args);
    connect(&ffmpegProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        this, &VideoToAsciiWidget::processFinished);
}

// 处理FFmpeg进程完成信号
void VideoToAsciiWidget::processFinished(int exitCode) {
    //处理成功返回值为0，否则为随机非0值
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

// 生成ASCII帧（先读取JPEG图像各像素点的灰度值，然后将灰度值映射到ASCII字符集）（运用opencv库）
void VideoToAsciiWidget::generateAsciiFrames() {
    if (asciiDir.exists()) asciiDir.removeRecursively();
    asciiDir.mkpath(".");

    // 获取所有JPEG图像文件
    QStringList images = tempDir.entryList({ "*.jpg" }, QDir::Files, QDir::Name);
    totalFrames = images.size();
    if (totalFrames == 0) return;

    const char asciiChars[] = "@%#*+=-:. ";// ASCII字符集，后续会根据灰度值映射到这些字符上
    const int asciiCharsCount = sizeof(asciiChars) - 1;// 获取字符集大小，不包括终止符
    int processedFrames = 0;// 处理的帧数

    // 根据用户选择决定分辨率
    int targetWidth = reduceResolutionCheck->isChecked() ? 100 : 150;
    int targetHeight = reduceResolutionCheck->isChecked() ? 40 : 60;

    // 遍历所有图像文件并转换为ASCII
    foreach(const QString & imageName, images) {
        if (conversionWatcher.isCanceled()) break;

        QString imagePath = tempDir.filePath(imageName);
        cv::Mat image = cv::imread(imagePath.toStdString(), cv::IMREAD_GRAYSCALE);// 读取图像每一个像素的灰度值

        if (!image.empty()) {
            // 调整图像大小
            cv::Mat resized;
            cv::resize(image, resized, cv::Size(targetWidth, targetHeight));

            QString ascii;
            ascii.reserve((resized.cols + 1) * resized.rows); // 预分配内存

            //遍历每行每列像素点，将灰度值映射到ASCII字符
            for (int y = 0; y < resized.rows; ++y) {
                for (int x = 0; x < resized.cols; ++x) {
                    uchar pixel = resized.at<uchar>(y, x);// 获取灰度值
                    int index = (pixel * asciiCharsCount) / 256;//灰度值的范围是0-255，将其映射到ASCII字符集的索引
                    ascii += asciiChars[index];//通过刚才计算的索引获取对应的ASCII字符
                }
                ascii += '\n';//一行读取结束后添加换行符，再读取下一行
            }

            // 保存到文件
            QString txtFileName = imageName + ".txt";
            QFile file(asciiDir.filePath(txtFileName));
            if (file.open(QIODevice::WriteOnly)) {
                file.write(ascii.toUtf8());
                asciiFrameFiles.append(asciiDir.filePath(txtFileName));
            }
        }

        //每次处理玩一帧图像后更新进度条
        processedFrames++;//统计处理的帧数
        int progress = (processedFrames * 100) / totalFrames;// 计算进度百分比
        QMetaObject::invokeMethod(this, "updateProgress", Qt::QueuedConnection, Q_ARG(int, progress));// 更新进度条
    }
}

QString VideoToAsciiWidget::imageToAscii(const cv::Mat& image) {
    return QString(); // 不再使用
}

// 更新进度条
void VideoToAsciiWidget::updateProgress(int value) {
    progressBar->setValue(value);
}

// 处理转换完成信号
void VideoToAsciiWidget::conversionCompleted() {
    // 转换完成，更新UI状态，启用按钮
    conversionRunning = false;
    convertBtn->setEnabled(true);
    playBtn->setEnabled(!asciiFrameFiles.isEmpty());

    //弹出提示框显示转换结果
    if (!conversionWatcher.isCanceled() && !asciiFrameFiles.isEmpty()) {
        QMessageBox::information(this, "Completed",
            QString("Conversion successful! %1 frames processed.").arg(asciiFrameFiles.count()));
        progressBar->setFormat(QString("Ready: %1 frames").arg(asciiFrameFiles.count()));
    }
    else {
        QMessageBox::warning(this, "Cancelled", "Conversion was cancelled");
        progressBar->setFormat("Cancelled");
    }

    // 清理临时图像文件
    if (tempDir.exists()) {
        tempDir.removeRecursively();
    }
}

//调整帧延迟（即每一帧的显示时间间隔），以控制播放速度
void VideoToAsciiWidget::adjustFrameDelay(int value) {
    frameDelay = value;
    fpsLabel->setText(QString("Playback Speed: %1 FPS").arg(1000.0 / value, 0, 'f', 1));

    if (isPlaying) {
        playTimer->setInterval(frameDelay);
    }
}

// 切换播放状态（播放/暂停）
void VideoToAsciiWidget::togglePlayback() {
    if (asciiFrameFiles.isEmpty()) {
        QMessageBox::warning(this, "Error", "No frames to play. Convert a video first.");
        return;
    }

    // 如果当前正在播放，则显示暂停按钮；如果未播放，则显示开始播放按钮
    if (!isPlaying) {
        currentFrame = 0;
        skippedFrames = 0;
        skipFactor = 0;
        playTimer->start(frameDelay);
        playBtn->setText("? Pause");
        isPlaying = true;
    }
    else {
        playTimer->stop();
        playBtn->setText("? Play");
        isPlaying = false;
    }
}

// 播放ASCII动画
void VideoToAsciiWidget::showNextFrame() {
    if (asciiFrameFiles.isEmpty()) {
        playTimer->stop();
        return;
    }

    // 帧跳过逻辑：如果渲染落后，跳过一些帧
    if (skipFactor > 0 && skippedFrames < skipFactor) {
        skippedFrames++;
        currentFrame = (currentFrame + 1) % asciiFrameFiles.count();
        return;
    }
    skippedFrames = 0;

    if (currentFrame >= asciiFrameFiles.count()) {
        currentFrame = 0;
    }

    QFile file(asciiFrameFiles.at(currentFrame));
    if (file.open(QIODevice::ReadOnly)) {
        QByteArray content = file.readAll();

        // 测量渲染时间
        QElapsedTimer timer;
        timer.start();
        asciiDisplay->setPlainText(QString::fromUtf8(content));
        int renderTime = timer.elapsed();

        // 动态调整帧跳过：如果渲染时间超过帧间隔的80%
        if (renderTime > frameDelay * 0.8) {
            skipFactor = qMin(skipFactor + 1, 5); // 最多跳过5帧
        }
        else if (skipFactor > 0 && renderTime < frameDelay * 0.3) {
            skipFactor = qMax(skipFactor - 1, 0);
        }
    }

    currentFrame++;

    // 循环播放
    if (currentFrame >= asciiFrameFiles.count()) {
        currentFrame = 0;
    }
}