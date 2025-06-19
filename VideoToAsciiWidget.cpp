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
    // ����������ʱĿ¼��һ�����ڴ洢FFmpeg���ɵ�ͼ��֡����һ�����ڴ洢ASCII�ı��ļ�
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");//�����߼�����׺���ϵ�ǰʱ�䣬����Ŀ¼����ͻ
    tempDir = QDir("Temp_" + timestamp);//��λͼ��֡Ŀ¼λ�ã����ں�������FFmpeg
    asciiDir = QDir("Ascii_" + timestamp);//��λASCII�ı�Ŀ¼λ�ã����ں�������opencv

    // ȷ��Ŀ¼����
    if (!tempDir.exists()) tempDir.mkpath(".");
    if (!asciiDir.exists()) asciiDir.mkpath(".");

    setupUI();
    playTimer = new QTimer(this);// ����һ����ʱ�����ڲ���ASCII�����������Ʋ����ٶ�
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
    QVBoxLayout* mainLayout = new QVBoxLayout;

    // ����������壨��������ϸ�µ����򻮷֣�
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

    // �ֱ���ѡ���ѡ���Ƿ񽵵ͷֱ�������߲���������
    reduceResolutionCheck = new QCheckBox("Reduce resolution for smoother playback");
	reduceResolutionCheck->setChecked(true); // Ĭ��ѡ�У����ͷֱ�������߲���������
    controlLayout->addWidget(reduceResolutionCheck);

    // ֡�ʿ���
    QHBoxLayout* fpsLayout = new QHBoxLayout;
    fpsLabel = new QLabel("Playback Speed: 30 FPS");
    frameDelaySlider = new QSlider(Qt::Horizontal);
    frameDelaySlider->setRange(10, 200); // ����ÿһ֡�����ʱ�������Ӷ����Ʋ�������
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

    // ������������������Ϣ��ʾ��
    progressBar = new QProgressBar;
    progressBar->setRange(0, 100);
    progressBar->setTextVisible(true);
    progressBar->setFormat("Idle");
    mainLayout->addWidget(progressBar);

    // ASCII��ʾ���򣨸���Ϊ����Ч��QPlainTextEdit�ؼ���֧�ִ��ı���ʾ�͸������Ⱦ�ٶȣ�
    QGroupBox* displayGroup = new QGroupBox("ASCII Animation");//����
    QVBoxLayout* displayLayout = new QVBoxLayout;

    asciiDisplay = new QPlainTextEdit;
    asciiDisplay->setReadOnly(true);// ����Ϊֻ��ģʽ����ֹ�û��༭����
    asciiDisplay->setFont(QFont("Courier", 6)); // ʹ�ø�С�����壬������Ⱦѹ��
    asciiDisplay->setLineWrapMode(QPlainTextEdit::NoWrap);// �����Զ����У���ֹ�򴰿ڴ�С�仯���µ���Ⱦ����
    asciiDisplay->setCenterOnScroll(true); // ���ù��������ԣ�ȷ���ı����Թ���

    displayLayout->addWidget(asciiDisplay);
    displayGroup->setLayout(displayLayout);
    mainLayout->addWidget(displayGroup, 1);

    setLayout(mainLayout);

    // �����źŲ�
    connect(browseBtn, &QPushButton::clicked, this, &VideoToAsciiWidget::browseVideo);
    connect(convertBtn, &QPushButton::clicked, this, &VideoToAsciiWidget::startConversion);
    connect(playBtn, &QPushButton::clicked, this, &VideoToAsciiWidget::togglePlayback);
    connect(frameDelaySlider, &QSlider::valueChanged, this, &VideoToAsciiWidget::adjustFrameDelay);
}

// ѡ����Ƶ�ļ�
void VideoToAsciiWidget::browseVideo() {
    videoPath = QFileDialog::getOpenFileName(this, "Select Video", "",
        "Video Files (*.mp4 *.avi *.mov *.mkv)");
    videoPathLabel->setText(videoPath.isEmpty() ? "No video selected!" : QFileInfo(videoPath).fileName());
    playBtn->setEnabled(false);
    playBtn->setText("? Play");
    isPlaying = false;
}

// ��ʼת��
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

//����FFmpeg������Ƶת��
void VideoToAsciiWidget::convertWithFFmpeg() {
    // ������ļ�
    if (tempDir.exists()) tempDir.removeRecursively();
    tempDir.mkpath(".");

    QStringList args;
    args << "-y" // ��������ļ�
        << "-i" << videoPath
        << "-vf" << "fps=30" // ����30fps
        << "-q:v" << "2" // ����JPEG����
        << tempDir.filePath("frame_%05d.jpg");

    ffmpegProcess.start("ffmpeg", args);
    connect(&ffmpegProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        this, &VideoToAsciiWidget::processFinished);
}

// ����FFmpeg��������ź�
void VideoToAsciiWidget::processFinished(int exitCode) {
    //����ɹ�����ֵΪ0������Ϊ�����0ֵ
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

// ����ASCII֡���ȶ�ȡJPEGͼ������ص�ĻҶ�ֵ��Ȼ�󽫻Ҷ�ֵӳ�䵽ASCII�ַ�����������opencv�⣩
void VideoToAsciiWidget::generateAsciiFrames() {
    if (asciiDir.exists()) asciiDir.removeRecursively();
    asciiDir.mkpath(".");

    // ��ȡ����JPEGͼ���ļ�
    QStringList images = tempDir.entryList({ "*.jpg" }, QDir::Files, QDir::Name);
    totalFrames = images.size();
    if (totalFrames == 0) return;

    const char asciiChars[] = "@%#*+=-:. ";// ASCII�ַ�������������ݻҶ�ֵӳ�䵽��Щ�ַ���
    const int asciiCharsCount = sizeof(asciiChars) - 1;// ��ȡ�ַ�����С����������ֹ��
    int processedFrames = 0;// �����֡��

    // �����û�ѡ������ֱ���
    int targetWidth = reduceResolutionCheck->isChecked() ? 100 : 150;
    int targetHeight = reduceResolutionCheck->isChecked() ? 40 : 60;

    // ��������ͼ���ļ���ת��ΪASCII
    foreach(const QString & imageName, images) {
        if (conversionWatcher.isCanceled()) break;

        QString imagePath = tempDir.filePath(imageName);
        cv::Mat image = cv::imread(imagePath.toStdString(), cv::IMREAD_GRAYSCALE);// ��ȡͼ��ÿһ�����صĻҶ�ֵ

        if (!image.empty()) {
            // ����ͼ���С
            cv::Mat resized;
            cv::resize(image, resized, cv::Size(targetWidth, targetHeight));

            QString ascii;
            ascii.reserve((resized.cols + 1) * resized.rows); // Ԥ�����ڴ�

            //����ÿ��ÿ�����ص㣬���Ҷ�ֵӳ�䵽ASCII�ַ�
            for (int y = 0; y < resized.rows; ++y) {
                for (int x = 0; x < resized.cols; ++x) {
                    uchar pixel = resized.at<uchar>(y, x);// ��ȡ�Ҷ�ֵ
                    int index = (pixel * asciiCharsCount) / 256;//�Ҷ�ֵ�ķ�Χ��0-255������ӳ�䵽ASCII�ַ���������
                    ascii += asciiChars[index];//ͨ���ղż����������ȡ��Ӧ��ASCII�ַ�
                }
                ascii += '\n';//һ�ж�ȡ��������ӻ��з����ٶ�ȡ��һ��
            }

            // ���浽�ļ�
            QString txtFileName = imageName + ".txt";
            QFile file(asciiDir.filePath(txtFileName));
            if (file.open(QIODevice::WriteOnly)) {
                file.write(ascii.toUtf8());
                asciiFrameFiles.append(asciiDir.filePath(txtFileName));
            }
        }

        //ÿ�δ�����һ֡ͼ�����½�����
        processedFrames++;//ͳ�ƴ����֡��
        int progress = (processedFrames * 100) / totalFrames;// ������Ȱٷֱ�
        QMetaObject::invokeMethod(this, "updateProgress", Qt::QueuedConnection, Q_ARG(int, progress));// ���½�����
    }
}

QString VideoToAsciiWidget::imageToAscii(const cv::Mat& image) {
    return QString(); // ����ʹ��
}

// ���½�����
void VideoToAsciiWidget::updateProgress(int value) {
    progressBar->setValue(value);
}

// ����ת������ź�
void VideoToAsciiWidget::conversionCompleted() {
    // ת����ɣ�����UI״̬�����ð�ť
    conversionRunning = false;
    convertBtn->setEnabled(true);
    playBtn->setEnabled(!asciiFrameFiles.isEmpty());

    //������ʾ����ʾת�����
    if (!conversionWatcher.isCanceled() && !asciiFrameFiles.isEmpty()) {
        QMessageBox::information(this, "Completed",
            QString("Conversion successful! %1 frames processed.").arg(asciiFrameFiles.count()));
        progressBar->setFormat(QString("Ready: %1 frames").arg(asciiFrameFiles.count()));
    }
    else {
        QMessageBox::warning(this, "Cancelled", "Conversion was cancelled");
        progressBar->setFormat("Cancelled");
    }

    // ������ʱͼ���ļ�
    if (tempDir.exists()) {
        tempDir.removeRecursively();
    }
}

//����֡�ӳ٣���ÿһ֡����ʾʱ���������Կ��Ʋ����ٶ�
void VideoToAsciiWidget::adjustFrameDelay(int value) {
    frameDelay = value;
    fpsLabel->setText(QString("Playback Speed: %1 FPS").arg(1000.0 / value, 0, 'f', 1));

    if (isPlaying) {
        playTimer->setInterval(frameDelay);
    }
}

// �л�����״̬������/��ͣ��
void VideoToAsciiWidget::togglePlayback() {
    if (asciiFrameFiles.isEmpty()) {
        QMessageBox::warning(this, "Error", "No frames to play. Convert a video first.");
        return;
    }

    // �����ǰ���ڲ��ţ�����ʾ��ͣ��ť�����δ���ţ�����ʾ��ʼ���Ű�ť
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

// ����ASCII����
void VideoToAsciiWidget::showNextFrame() {
    if (asciiFrameFiles.isEmpty()) {
        playTimer->stop();
        return;
    }

    // ֡�����߼��������Ⱦ�������һЩ֡
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

        // ������Ⱦʱ��
        QElapsedTimer timer;
        timer.start();
        asciiDisplay->setPlainText(QString::fromUtf8(content));
        int renderTime = timer.elapsed();

        // ��̬����֡�����������Ⱦʱ�䳬��֡�����80%
        if (renderTime > frameDelay * 0.8) {
            skipFactor = qMin(skipFactor + 1, 5); // �������5֡
        }
        else if (skipFactor > 0 && renderTime < frameDelay * 0.3) {
            skipFactor = qMax(skipFactor - 1, 0);
        }
    }

    currentFrame++;

    // ѭ������
    if (currentFrame >= asciiFrameFiles.count()) {
        currentFrame = 0;
    }
}