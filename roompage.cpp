#include "roompage.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QFrame>

RoomPage::RoomPage(QWidget *parent): QWidget(parent){
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(30, 20, 30, 20);
    mainLayout->setSpacing(15);

    // ===== 顶部 =====
    auto* topLayout = new QHBoxLayout;

    auto* title = new QLabel("异地同步影院");
    auto* username = new QLabel("用户：Baunaa");

    title->setObjectName("pageTitle");
    username->setObjectName("username");

    topLayout->addWidget(title);
    topLayout->addStretch();
    topLayout->addWidget(username);

    mainLayout->addLayout(topLayout);

    // ===== 主体 =====
    auto* contentLayout = new QHBoxLayout;

    // ===== 左侧 =====
    auto* leftLayout = new QVBoxLayout;

    auto* roomTitle = new QLabel("我的房间");
    auto* roomList = new QListWidget;
    auto* createButton = new QPushButton("创建房间");

    roomTitle->setObjectName("roomTitle");

    roomList->addItem("房间 1001");
    roomList->addItem("房间 1002");
    roomList->addItem("房间 1003");

    roomList->setMinimumWidth(180);

    leftLayout->addWidget(roomTitle);
    leftLayout->addWidget(roomList);
    leftLayout->addWidget(createButton);

    // ===== 右侧 =====
    auto* rightLayout = new QVBoxLayout;

    auto* roomInfoTitle = new QLabel("当前房间");
    auto* roomId = new QLabel("房间：1001");
    auto* roomOwner = new QLabel("房主：Alice");
    auto* onlineCount = new QLabel("在线人数：2");

    auto* enterButton = new QPushButton("进入影院");

    roomInfoTitle->setObjectName("roomInfoTitle");

    rightLayout->addWidget(roomInfoTitle);
    rightLayout->addSpacing(20);
    rightLayout->addWidget(roomId);
    rightLayout->addWidget(roomOwner);
    rightLayout->addWidget(onlineCount);
    rightLayout->addStretch();
    rightLayout->addWidget(enterButton);

    // ===== 加入主体 =====
    contentLayout->addLayout(leftLayout);
    contentLayout->addLayout(rightLayout);

    contentLayout->setStretch(0, 1);
    contentLayout->setStretch(1, 3);

    mainLayout->addLayout(contentLayout);


    // ===== 信号 =====
    connect(enterButton, &QPushButton::clicked, this, &RoomPage::enterCinema);
}