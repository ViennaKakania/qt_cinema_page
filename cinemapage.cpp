#include "cinemapage.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QSlider>
#include <QFrame>

CinemaPage::CinemaPage(QWidget *parent): QWidget(parent){
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 15, 20, 15);
    mainLayout->setSpacing(10);

    // ===== 顶部 =====
    auto* topLayout = new QHBoxLayout;

    backButton = new QPushButton("← 返回房间");

    auto *title = new QLabel("异地影院");
    roomInfo = new QLabel("房间：1001   在线：2人");

    title->setObjectName("pageTitle");
    roomInfo->setObjectName("roomInfo");

    topLayout->addWidget(backButton);
    topLayout->addSpacing(20);
    topLayout->addWidget(title);
    topLayout->addStretch();
    topLayout->addWidget(roomInfo);

    mainLayout->addLayout(topLayout);

    // ===== 中间区域 =====
    auto* contentLayout = new QHBoxLayout;

    auto* videoArea = new QFrame;
    videoArea->setObjectName("videoArea");
    videoArea->setFrameShape(QFrame::StyledPanel);

    auto* chatLayout = new QVBoxLayout;

    chatList = new QListWidget;

    chatList->addItem("Alice：你好");
    chatList->addItem("Bob：开始吧");
    chatList->addItem("Alice：等我一下");

    auto* messageLayout = new QHBoxLayout;

    messageEdit = new QLineEdit;
    sendButton = new QPushButton("发送");

    messageEdit->setPlaceholderText("输入消息...");

    messageLayout->addWidget(messageEdit);
    messageLayout->addWidget(sendButton);

    chatLayout->addWidget(chatList);
    chatLayout->addLayout(messageLayout);

    contentLayout->addWidget(videoArea);
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
        QString message = messageEdit->text();

        if(message.isEmpty()) return;

        chatList->addItem("我：" + message);
        messageEdit->clear();
    };
    connect(backButton, &QPushButton::clicked, this, &CinemaPage::backToRoom);
    connect(sendButton, &QPushButton::clicked, this, msgSignal);
    connect(messageEdit, &QLineEdit::returnPressed, sendButton, &QPushButton::click);
}