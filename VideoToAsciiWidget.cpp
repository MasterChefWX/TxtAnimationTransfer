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
    QVBoxLayout* layout = new QVBoxLayout;

    // �����������
    browseBtn = new QPushButton("Browse Video");
    convertBtn = new QPushButton("Start Conversion");
    playBtn = new QPushButton("Play Animation");
    videoPathLabel = new QLabel("No video selected!");

    //������
    progressBar = new QProgressBar;
    progressBar->setRange(0, 100);

	//ASCII��ʾ����
    asciiDisplay = new QTextBrowser;
    asciiDisplay->setFont(QFont("Courier", 8));
    asciiDisplay->setMinimumHeight(300);

    // �����źŲ�
    connect(browseBtn, &QPushButton::clicked, this, &VideoToAsciiWidget::browseVideo);
    connect(convertBtn, &QPushButton::clicked, this, &VideoToAsciiWidget::startConversion);
    connect(playBtn, &QPushButton::clicked, [this] {
        currentFrame = 0;
        playTimer->start(33); // ~30 fps
        });

    // ���ò��Ű�ťֱ��ת�����
    playBtn->setEnabled(false);

	// ��������
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

// ѡ����Ƶ�ļ�
void VideoToAsciiWidget::browseVideo() {
    videoPath = QFileDialog::getOpenFileName(this, "Select Video", "",
        "Video Files (*.mp4 *.avi *.mov *.mkv)");
    videoPathLabel->setText(videoPath.isEmpty() ? "No video selected!" : QFileInfo(videoPath).fileName());
    playBtn->setEnabled(false);
}

// ��ʼת����ƵΪASCII����
void VideoToAsciiWidget::startConversion() {
    if (videoPath.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select a video file");
        return;
    }

    if (conversionRunning) {
        QMessageBox::warning(this, "Error", "Conversion already in progress");
        return;
    }

	// ���ð�ť,���ý�����
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
    
	// Ԥ����FFmpeg�����в���
    QStringList args;
    args << "-y" // ��������ļ�
        << "-i" << videoPath
        << "-vf" << "fps=30,scale=200:100" // ��ǰ��С�ߴ���ٴ�����
        << "-q:v" << "2" // ����JPEG����
        << tempDir.filePath("frame_%05d.jpg");

	// ����FFmpeg����
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

	// ��������ͼ���ļ���ת��ΪASCII
    foreach(const QString & imageName, images) {
        if (conversionWatcher.isCanceled()) break;

        QString imagePath = tempDir.filePath(imageName);
		cv::Mat image = cv::imread(imagePath.toStdString(), cv::IMREAD_GRAYSCALE);// ��ȡͼ��ÿһ�����صĻҶ�ֵ

        if (!image.empty()) {
            QString ascii;
            ascii.reserve((image.cols + 1) * image.rows); // Ԥ�����ڴ�

			//����ÿ��ÿ�����ص㣬���Ҷ�ֵӳ�䵽ASCII�ַ�
            for (int y = 0; y < image.rows; ++y) {
                for (int x = 0; x < image.cols; ++x) {
					uchar pixel = image.at<uchar>(y, x);// ��ȡ�Ҷ�ֵ
					int index = (pixel * asciiCharsCount) / 256;//�Ҷ�ֵ�ķ�Χ��0-255������ӳ�䵽ASCII�ַ���������
					ascii += asciiChars[index];//ͨ���ղż����������ȡ��Ӧ��ASCII�ַ�
                }
				ascii += '\n';//һ�ж�ȡ��������ӻ��з����ٶ�ȡ��һ��
            }

            asciiFrames.append(ascii);
            QFile file(asciiDir.filePath(imageName + ".txt"));
            if (file.open(QIODevice::WriteOnly)) {
                file.write(ascii.toUtf8());
            }
        }

		//ÿ�δ�����һ֡ͼ�����½�����
		processedFrames++;//ͳ�ƴ����֡��
		int progress = (processedFrames * 100) / totalFrames;// ������Ȱٷֱ�
		QMetaObject::invokeMethod(this, "updateProgress", Qt::QueuedConnection, Q_ARG(int, progress));// ���½�����
    }
}

QString VideoToAsciiWidget::imageToAscii(const cv::Mat& image) {
    // �Ѽ��ɵ�generateAsciiFrames���Ż�����
    return QString();
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
    playBtn->setEnabled(!asciiFrames.isEmpty());

	//������ʾ����ʾת�����
    if (!conversionWatcher.isCanceled() && !asciiFrames.isEmpty()) {
        QMessageBox::information(this, "Completed", "Conversion successful!");
    }
    else {
        QMessageBox::warning(this, "Cancelled", "Conversion was cancelled");
    }
}

// ����ASCII����
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