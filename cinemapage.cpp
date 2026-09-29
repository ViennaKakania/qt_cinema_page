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

CinemaPage::CinemaPage(QWidget *parent): QWidget(parent){
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 15, 20, 15);
    mainLayout->setSpacing(10);

    // ===== 顶部 =====
    auto* topLayout = new QHBoxLayout;

    backButton = new QPushButton("← 返回房间");

    auto *title = new QLabel("异地影院");

    title->setObjectName("pageTitle");

    topLayout->addWidget(backButton);
    topLayout->addSpacing(20);
    topLayout->addWidget(title);
    topLayout->addStretch();

    mainLayout->addLayout(topLayout);

    // ===== 中间区域 =====
    auto* contentLayout = new QHBoxLayout;

    videoLabel = new QLabel;
    videoLabel->setObjectName("videoArea");
    videoLabel->setAlignment(Qt::AlignCenter);
    videoLabel->setStyleSheet("background-color: black;");
    videoLabel->setMinimumSize(640, 360);

    auto* chatLayout = new QVBoxLayout;

    chatList = new QListWidget;

    auto* messageLayout = new QHBoxLayout;

    messageEdit = new QLineEdit;
    sendButton = new QPushButton("发送");

    messageEdit->setPlaceholderText("输入消息...");

    messageLayout->addWidget(messageEdit);
    messageLayout->addWidget(sendButton);

    chatLayout->addWidget(chatList);
    chatLayout->addLayout(messageLayout);

    contentLayout->addWidget(videoLabel);
    contentLayout->addLayout(chatLayout);

    contentLayout->setStretch(0, 3);
    contentLayout->setStretch(1, 1);

    mainLayout->addLayout(contentLayout);

    // ===== 播放控制 =====
    auto* controlLayout = new QHBoxLayout;

    playButton = new QPushButton("▶");

    progressSlider = new QSlider(Qt::Horizontal);
    progressSlider->setRange(0, 100);

    timeLabel = new QLabel("00:00 / 00:00");

    auto* volumeLabel = new QLabel("🔊");

    controlLayout->addWidget(playButton);
    controlLayout->addWidget(progressSlider, 1);
    controlLayout->addWidget(timeLabel);
    controlLayout->addWidget(volumeLabel);

    mainLayout->addLayout(controlLayout);



    // ===== 信号 =====
    auto msgSignal = [this](){
        QString message = messageEdit->text().trimmed();

        if(message.isEmpty()) return;

        emit sendChatMsg(message);
        chatList->addItem("我：" + message); // 自己的消息立即显示
        messageEdit->clear();  // 发消息的框清空
        chatList->scrollToBottom();
    };
    connect(backButton, &QPushButton::clicked, this, &CinemaPage::backToRoom);
    connect(sendButton, &QPushButton::clicked, this, msgSignal);
    connect(messageEdit, &QLineEdit::returnPressed, sendButton, &QPushButton::click);
    connect(playButton, &QPushButton::clicked, this, [this](){
                if(!player || !player->isOpened()) return;

                if(!playing){
                    playing = true;
                    playButton->setText("⏸");

                    if(!decodeThread->isRunning()){
                        decodeThread->start();
                    }
                    decodeThread->startPlayback();

                    playbackTimer->start();
                    progressTimer->start();
                }
                else{
                    playing = false;
                    playButton->setText("▶");

                    decodeThread->pausePlayback();

                     playbackTimer->stop();
                    progressTimer->stop();
                }
    });
    connect(progressSlider, &QSlider::sliderPressed, this, [this](){
                seeking = true;

                if(progressTimer) progressTimer->stop();
    });
    connect(progressSlider, &QSlider::sliderMoved, this, [this](int value){
        double seconds = static_cast<double>(value) / 1000.0;

        int totalSeconds = static_cast<int>(seconds);

        int minutes = totalSeconds / 60;
        int secs = totalSeconds % 60;

        timeLabel->setText(QString("%1:%2").arg(minutes).arg(secs, 2, 10, QChar('0')));
    });
    connect(progressSlider, &QSlider::sliderReleased, this, [this](){
        if (!player || !decodeThread) return;

        int value = progressSlider->value();

        double targetTime = static_cast<double>(value) / 1000.0;

        // 不再直接操作 FFmpeg
        decodeThread->requestSeek(targetTime);

        seeking = false;

        if(playing){
            progressTimer->start();
        }
    });


    // ===== FFmpeg播放器 =====
    player = new FFmpegPlayer();

    playbackTimer = new QTimer(this);
    progressTimer = new QTimer(this);

    playing = false;
    seeking = false;

    playbackTimer->setInterval(33);
    progressTimer->setInterval(100);

    if(!player->open("C:/Users/24394/Videos/NVIDIA/Valorant/Valorant 2026.01.01 - 17.32.08.01.mp4")){
        std::cerr << "video open failed" << std::endl;
        return;
    }

    if(!player->initAudioOutput()){
        std::cerr << "Audio output initialization failed." << std::endl;
    }

    decodeThread = new FFmpegDecodeThread(player);

    double duration = player->getDuration();

    progressSlider->setRange(0, static_cast<int>(duration * 1000));
    progressSlider->setValue(0);


    connect(playbackTimer, &QTimer::timeout, this,  &CinemaPage::displayNextFrame);
    connect(progressTimer, &QTimer::timeout, this, [this](){
            if(!player || seeking) return;

            double current = player->getCurrentTime();

            progressSlider->setValue(static_cast<int>(current * 1000));

            int totalSeconds = static_cast<int>(current);

            int minutes = totalSeconds / 60;
            int seconds = totalSeconds % 60;

            timeLabel->setText(QString("%1:%2").arg(minutes).arg(seconds, 2, 10, QChar('0')));
    });
}

CinemaPage::~CinemaPage(){
    if(playbackTimer) playbackTimer->stop();

    if(progressTimer) progressTimer->stop();

    if(decodeThread){
        decodeThread->stopThread();
        decodeThread->wait();

        delete decodeThread;
        decodeThread = nullptr;
    }

    if (player){
        player->close();

        delete player;
        player = nullptr;
    }
}

void CinemaPage::addChatMessage(const QString& message){
    chatList->addItem(message);
    chatList->scrollToBottom();
}

void CinemaPage::displayNextFrame(){
    if (!decodeThread) return;

    QImage image;
    double pts = 0.0;

    if(!decodeThread->getNextFrame(image, pts)) return;

    if(image.isNull()) return;

    QPixmap pixmap = QPixmap::fromImage(image);

    videoLabel->setPixmap(pixmap.scaled(videoLabel->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
}