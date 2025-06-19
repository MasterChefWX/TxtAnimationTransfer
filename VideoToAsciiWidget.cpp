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
    QVBoxLayout* layout = new QVBoxLayout;

    // 创建控制面板
    browseBtn = new QPushButton("Browse Video");
    convertBtn = new QPushButton("Start Conversion");
    playBtn = new QPushButton("Play Animation");
    videoPathLabel = new QLabel("No video selected!");

    //进度条
    progressBar = new QProgressBar;
    progressBar->setRange(0, 100);

	//ASCII显示区域
    asciiDisplay = new QTextBrowser;
    asciiDisplay->setFont(QFont("Courier", 8));
    asciiDisplay->setMinimumHeight(300);

    // 连接信号槽
    connect(browseBtn, &QPushButton::clicked, this, &VideoToAsciiWidget::browseVideo);
    connect(convertBtn, &QPushButton::clicked, this, &VideoToAsciiWidget::startConversion);
    connect(playBtn, &QPushButton::clicked, [this] {
        currentFrame = 0;
        playTimer->start(33); // ~30 fps
        });

    // 禁用播放按钮直到转换完成
    playBtn->setEnabled(false);

	// 布局设置
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

// 选择视频文件
void VideoToAsciiWidget::browseVideo() {
    videoPath = QFileDialog::getOpenFileName(this, "Select Video", "",
        "Video Files (*.mp4 *.avi *.mov *.mkv)");
    videoPathLabel->setText(videoPath.isEmpty() ? "No video selected!" : QFileInfo(videoPath).fileName());
    playBtn->setEnabled(false);
}

// 开始转换视频为ASCII动画
void VideoToAsciiWidget::startConversion() {
    if (videoPath.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select a video file");
        return;
    }

    if (conversionRunning) {
        QMessageBox::warning(this, "Error", "Conversion already in progress");
        return;
    }

	// 禁用按钮,重置进度条
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
    
	// 预编译FFmpeg命令行参数
    QStringList args;
    args << "-y" // 覆盖输出文件
        << "-i" << videoPath
        << "-vf" << "fps=30,scale=200:100" // 提前缩小尺寸减少处理量
        << "-q:v" << "2" // 控制JPEG质量
        << tempDir.filePath("frame_%05d.jpg");

	// 启动FFmpeg进程
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

	// 遍历所有图像文件并转换为ASCII
    foreach(const QString & imageName, images) {
        if (conversionWatcher.isCanceled()) break;

        QString imagePath = tempDir.filePath(imageName);
		cv::Mat image = cv::imread(imagePath.toStdString(), cv::IMREAD_GRAYSCALE);// 读取图像每一个像素的灰度值

        if (!image.empty()) {
            QString ascii;
            ascii.reserve((image.cols + 1) * image.rows); // 预分配内存

			//遍历每行每列像素点，将灰度值映射到ASCII字符
            for (int y = 0; y < image.rows; ++y) {
                for (int x = 0; x < image.cols; ++x) {
					uchar pixel = image.at<uchar>(y, x);// 获取灰度值
					int index = (pixel * asciiCharsCount) / 256;//灰度值的范围是0-255，将其映射到ASCII字符集的索引
					ascii += asciiChars[index];//通过刚才计算的索引获取对应的ASCII字符
                }
				ascii += '\n';//一行读取结束后添加换行符，再读取下一行
            }

            asciiFrames.append(ascii);
            QFile file(asciiDir.filePath(imageName + ".txt"));
            if (file.open(QIODevice::WriteOnly)) {
                file.write(ascii.toUtf8());
            }
        }

		//每次处理玩一帧图像后更新进度条
		processedFrames++;//统计处理的帧数
		int progress = (processedFrames * 100) / totalFrames;// 计算进度百分比
		QMetaObject::invokeMethod(this, "updateProgress", Qt::QueuedConnection, Q_ARG(int, progress));// 更新进度条
    }
}

QString VideoToAsciiWidget::imageToAscii(const cv::Mat& image) {
    // 已集成到generateAsciiFrames中优化性能
    return QString();
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
    playBtn->setEnabled(!asciiFrames.isEmpty());

	//弹出提示框显示转换结果
    if (!conversionWatcher.isCanceled() && !asciiFrames.isEmpty()) {
        QMessageBox::information(this, "Completed", "Conversion successful!");
    }
    else {
        QMessageBox::warning(this, "Cancelled", "Conversion was cancelled");
    }
}

// 播放ASCII动画
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