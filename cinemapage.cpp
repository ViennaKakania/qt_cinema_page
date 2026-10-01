#include "cinemapage.h"
#include "FFmpegDecodeThread.h"

#include <iostream>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QSlider>
#include <QFrame>
#include <QPixmap>
#include <QFileDialog>

CinemaPage::CinemaPage(QWidget* parent) : QWidget(parent){
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 15, 20, 15);
    mainLayout->setSpacing(10);

    // ===== 顶部 =====
    auto* topLayout = new QHBoxLayout;
    backButton = new QPushButton("← 返回房间");
    auto* title = new QLabel("异地影院");
    title->setObjectName("pageTitle");
    topLayout->addWidget(backButton);
    topLayout->addSpacing(20);
    topLayout->addWidget(title);
    topLayout->addStretch();
    mainLayout->addLayout(topLayout);

    // ===== 视频区（用容器承载 VideoWidget + 叠加提示） =====
    auto* videoBox = new QWidget;
    auto* videoBoxLayout = new QVBoxLayout(videoBox);
    videoBoxLayout->setContentsMargins(0, 0, 0, 0);

    videoWidget = new VideoWidget;
    videoBoxLayout->addWidget(videoWidget);

    overlayLabel = new QLabel(videoBox);
    overlayLabel->setAlignment(Qt::AlignCenter);
    overlayLabel->setStyleSheet(
        "color:#DDDDDD; background:rgba(0,0,0,160); font-size:14px; padding:10px;");
    overlayLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    overlayLabel->hide();

    auto* chatLayout = new QVBoxLayout;
    chatList = new QListWidget;
    auto* messageLayout = new QHBoxLayout;
    messageEdit = new QLineEdit;
    sendButton  = new QPushButton("发送");
    messageEdit->setPlaceholderText("输入消息...");
    messageLayout->addWidget(messageEdit);
    messageLayout->addWidget(sendButton);
    chatLayout->addWidget(chatList);
    chatLayout->addLayout(messageLayout);

    auto* contentLayout = new QHBoxLayout;
    contentLayout->addWidget(videoBox);
    contentLayout->addLayout(chatLayout);
    contentLayout->setStretch(0, 3);
    contentLayout->setStretch(1, 1);
    mainLayout->addLayout(contentLayout, 1);

    // ===== 控制栏 =====
    auto* controlLayout = new QHBoxLayout;
    openButton     = new QPushButton("打开影片");
    playButton     = new QPushButton("▶");
    progressSlider = new QSlider(Qt::Horizontal);
    progressSlider->setRange(0, 1000);
    progressSlider->setEnabled(false);
    timeLabel = new QLabel("00:00 / 00:00");
    auto* volumeLabel = new QLabel("音量");
    volumeLabel->setStyleSheet("color:#888;");

    controlLayout->addWidget(openButton);
    controlLayout->addWidget(playButton);
    controlLayout->addWidget(progressSlider, 1);
    controlLayout->addWidget(timeLabel);
    controlLayout->addWidget(volumeLabel);
    mainLayout->addLayout(controlLayout);

    // ===== 对象与状态 =====
    player       = new FFmpegPlayer();
    decodeThread = new FFmpegDecodeThread(player);

    playbackTimer = new QTimer(this);
    progressTimer = new QTimer(this);
    playbackTimer->setInterval(15);     // 约 66fps 上限；实际由主时钟决定刷不刷新
    progressTimer->setInterval(200);

    playButton->setEnabled(false);

    // ===== 信号 =====
    auto sendMsg = [this](){
        QString message = messageEdit->text().trimmed();
        if(message.isEmpty()) return;
        emit sendChatMsg(message);
        chatList->addItem("我：" + message);
        messageEdit->clear();
        chatList->scrollToBottom();
    };
    connect(backButton, &QPushButton::clicked, this, &CinemaPage::backToRoom);
    connect(sendButton, &QPushButton::clicked, this, sendMsg);
    connect(messageEdit, &QLineEdit::returnPressed, sendButton, &QPushButton::click);

    connect(openButton, &QPushButton::clicked, this, [this](){
        openVideo(QString());                     // 空路径 → 弹文件选择框
    });

    connect(playButton, &QPushButton::clicked, this, [this](){
        if(!videoReady) return;

        if(!playing){
            playing = true;
            playButton->setText("⏸");

            if(!decodeThread->isRunning()) decodeThread->start();
            decodeThread->startPlayback();         // 内部会调 player->play()

            playbackTimer->start();
            progressTimer->start();
        }else{
            playing = false;
            playButton->setText("▶");

            decodeThread->pausePlayback();         // 内部会调 player->pause()
            playbackTimer->stop();
            progressTimer->stop();
        }
    });

    // ===== 拖动条 =====
    connect(progressSlider, &QSlider::sliderPressed, this, [this](){
        if(!videoReady) return;
        userDragging = true;
        progressTimer->stop();
    });

    connect(progressSlider, &QSlider::sliderMoved, this, [this](int value){
        int secs = value / 1000;
        int total = static_cast<int>(player->getDuration());
        timeLabel->setText(QString("%1:%2 / %3:%4")
                               .arg(secs / 60).arg(secs % 60, 2, 10, QChar('0'))
                               .arg(total / 60).arg(total % 60, 2, 10, QChar('0')));
    });

    connect(progressSlider, &QSlider::sliderReleased, this, [this](){
        userDragging = false;
        applySeek(progressSlider->value() / 1000.0);
    });

    // ===== 定时器 =====
    connect(playbackTimer, &QTimer::timeout, this, &CinemaPage::displayNextFrame);
    connect(progressTimer, &QTimer::timeout, this, [this](){
        if(!videoReady || userDragging) return;

        double current = player->getCurrentTime();
        double total   = player->getDuration();

        if(total > 0.0){
            progressSlider->setValue(static_cast<int>(current * 1000.0));
        }
        int c = static_cast<int>(current);
        int t = static_cast<int>(total);
        timeLabel->setText(QString("%1:%2 / %3:%4")
                               .arg(c / 60).arg(c % 60, 2, 10, QChar('0'))
                               .arg(t / 60).arg(t % 60, 2, 10, QChar('0')));
    });

    // ===== 解码线程回调（跨线程 → 自动排队到 GUI 线程） =====
    connect(decodeThread, &FFmpegDecodeThread::seekFinished, this, [this](bool wasPlaying){
        if(wasPlaying) player->play();       // 只有 GUI 线程碰 QAudioSink
        seeking = false;
        progressTimer->start();
    });

    connect(decodeThread, &FFmpegDecodeThread::decodeFinished, this, [this](){
        onPlaybackFinished();
    });

    setVideoStateMessage("请点击「打开影片」选择视频");
}


CinemaPage::~CinemaPage(){
    if(playbackTimer) playbackTimer->stop();
    if(progressTimer) progressTimer->stop();

    if(decodeThread){
        decodeThread->stopThread();
        decodeThread->wait();          // 必须先停线程，再关播放器
        delete decodeThread;
        decodeThread = nullptr;
    }
    if(player){
        player->close();
        delete player;
        player = nullptr;
    }

}

void CinemaPage::addChatMessage(const QString& message){
    chatList->addItem(message);
    chatList->scrollToBottom();
}

// ============================================================
// 渲染：以主时钟为准，一次 tick 只刷新一次画面
//   1) 拿到当前主时钟 clock
//   2) 把队列里所有 pts <= clock + 提前量的帧依次取出，只保留最后一帧 —— 中间的"迟到帧"直接跳过，等价于丢帧，但绝不会把队列头误判成过期
//   3) 队头还没到时间就直接返回，本次不刷新
// ============================================================
void CinemaPage::displayNextFrame(){
    if(!videoReady || !decodeThread || !player) return;

    const double clock = player->getCurrentTime();
    const double earlyTolerance = 0.010;      // 允许提前 10ms 显示

    QImage frame;
    double pts = 0.0;
    bool hasFrame = false;

    while(decodeThread->peekNextFrame(frame, pts)){
        if(pts > clock + earlyTolerance) break;      // 队头还没到时间，后面更晚

        if(!decodeThread->getNextFrame(frame, pts)) break;
        hasFrame = true;                             // 这帧可显示（有更新的会覆盖）
    }

    if(!hasFrame || frame.isNull()) return;

    videoWidget->setFrame(frame);
}


bool CinemaPage::openVideo(const QString& path){
    QString file = path;

    if(file.isEmpty()){
        file = QFileDialog::getOpenFileName(this, "选择影片", QString(), "视频文件 (*.mp4 *.mkv *.avi *.mov *.flv *.ts *.webm);;所有文件 (*)");
        if(file.isEmpty()) return false;
    }

    if(videoReady){
        // 换片：先停线程再重新打开
        decodeThread->stopThread();
        decodeThread->wait();
        playbackTimer->stop();
        progressTimer->stop();
        playing = false;
        playButton->setText("▶");
        player->close();
        videoReady = false;
        // 线程对象 stopThread 后不可复用，必须重建
        delete decodeThread;
        decodeThread = new FFmpegDecodeThread(player);
        connect(decodeThread, &FFmpegDecodeThread::seekFinished, this, [this](bool wasPlaying){
            if(wasPlaying) player->play();
            seeking = false;
            progressTimer->start();
        });
        connect(decodeThread, &FFmpegDecodeThread::decodeFinished, this, &CinemaPage::onPlaybackFinished);
    }

    // FFmpeg 在 Windows 上要求 UTF-8 路径
    QByteArray utf8 = file.toUtf8();
    if(!player->open(utf8.constData())){
        setVideoStateMessage(QString("视频打开失败：\n%1").arg(file));
        playButton->setEnabled(false);
        return false;
    }

    // 音频初始化失败不阻断视频播放
    if(!player->initAudioOutput()){
        qWarning() << "音频输出初始化失败，将静音播放";
    }

    onVideoOpened();
    return true;
}

void CinemaPage::onVideoOpened(){
    videoReady = true;
    overlayLabel->hide();

    double duration = player->getDuration();
    if(duration > 0.0){
        progressSlider->setEnabled(true);
        progressSlider->setRange(0, static_cast<int>(duration * 1000.0));
        progressSlider->setValue(0);
    }else{
        // duration 拿不到（AV_NOPTS_VALUE）：禁用拖动，别给用户一个划不动的条
        progressSlider->setEnabled(false);
        progressSlider->setRange(0, 1000);
    }

    playButton->setEnabled(true);

    if(!decodeThread->isRunning()) decodeThread->start();

    int t = static_cast<int>(duration);
    timeLabel->setText(QString("00:00 / %1:%2")
                           .arg(t / 60).arg(t % 60, 2, 10, QChar('0')));
}

void CinemaPage::setVideoStateMessage(const QString& text){
    overlayLabel->setText(text);
    overlayLabel->adjustSize();
    QRect g = videoWidget->geometry();
    overlayLabel->move(videoWidget->mapTo(this, QPoint(
                                                    (videoWidget->width()  - overlayLabel->width())  / 2,
                                                    (videoWidget->height() - overlayLabel->height()) / 2)));
    overlayLabel->raise();
    overlayLabel->show();
}


void CinemaPage::applySeek(double seconds){
    if(!videoReady || !decodeThread || !player) return;

    seeking = true;
    progressTimer->stop();
    userDragging = false;

    // 在 GUI 线程暂停音频（suspend 会让 processedUSecs 停止增长，时钟随之冻结）
    player->pause();

    // 交给解码线程执行真正的 av_seek_frame；
    // 它在内部记录 wasPlayingBeforeSeek，完成后通过 seekFinished(bool) 回传
    decodeThread->requestSeek(seconds);
}

void CinemaPage::onPlaybackFinished(){
    playing = false;
    playButton->setText("▶");
    player->pause();
    playbackTimer->stop();
    progressTimer->stop();

    double d = player->getDuration();
    if(d > 0.0) progressSlider->setValue(static_cast<int>(d * 1000.0));
    setVideoStateMessage("已播放完毕");
}

