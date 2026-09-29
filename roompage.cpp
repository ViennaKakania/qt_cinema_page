#include "roompage.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QLineEdit>
#include <QInputDialog>
#include <QMessageBox>
#include <QDebug>

RoomPage::RoomPage(QWidget *parent): QWidget(parent){
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(30, 20, 30, 20);
    mainLayout->setSpacing(15);

    // ===== 顶部 =====
    auto* topLayout = new QHBoxLayout;

    auto* title = new QLabel("异地同步影院");
    auto* usernameLabel = new QLabel("用户：未登录");

    title->setObjectName("pageTitle");
    usernameLabel->setObjectName("username");

    topLayout->addWidget(title);
    topLayout->addStretch();
    topLayout->addWidget(usernameLabel);

    mainLayout->addLayout(topLayout);

    // ===== 主体 =====
    auto* contentLayout = new QHBoxLayout;

    // ===== 左侧 =====
    auto* leftLayout = new QVBoxLayout;

    auto *roomTitle = new QLabel("房间列表");
    roomList = new QListWidget;
    roomList->setMinimumWidth(300);

    createButton = new QPushButton("创建房间");
    joinButton = new QPushButton("加入房间");

    leftLayout->addWidget(roomTitle);
    leftLayout->addWidget(roomList);

    auto* leftButtonLayout = new QHBoxLayout;
    leftButtonLayout->addWidget(createButton);
    leftButtonLayout->addWidget(joinButton);

    leftLayout->addLayout(leftButtonLayout);


    // ===== 右侧 =====
    auto* rightLayout = new QVBoxLayout;

    auto* roomInfoTitle = new QLabel("当前房间");

    roomIdLabel = new QLabel("房间：1001");
    roomNameLabel = new QLabel("名称：初始大厅");
    // roomOwnerLabel = new QLabel("房主：-");  暂时不做房主
    onlineCountLabel = new QLabel("在线人数：0");

    leaveButton = new QPushButton("离开房间");
    enterButton = new QPushButton("进入影院");

    roomInfoTitle->setObjectName("roomInfoTitle");

    rightLayout->addWidget(roomInfoTitle);
    rightLayout->addSpacing(20);

    rightLayout->addWidget(roomIdLabel);
    rightLayout->addWidget(roomNameLabel);
    // rightLayout->addWidget(roomOwnerLabel);  暂时不做房主
    rightLayout->addWidget(onlineCountLabel);

    rightLayout->addStretch();

    rightLayout->addWidget(leaveButton);
    rightLayout->addWidget(enterButton);


    // ===== 加入主体 =====
    contentLayout->addLayout(leftLayout);
    contentLayout->addLayout(rightLayout);

    contentLayout->setStretch(0, 2);
    contentLayout->setStretch(1, 1);

    mainLayout->addLayout(contentLayout);



    // ================= 创建房间 =================
    connect(createButton, &QPushButton::clicked, this, [this](){
                bool ok = false;

                int roomId = QInputDialog::getInt(this, "创建房间", "请输入房间号：", 1001, 1, 999999, 1, &ok);

                if(!ok) return;

                QString roomName = QInputDialog::getText( this, "创建房间", "请输入房间名称：", QLineEdit::Normal, "", &ok);

                if(!ok || roomName.trimmed().isEmpty()) return;

                emit createRoom(roomId, roomName.trimmed());
            });


    // ================= 加入房间 =================
    connect(joinButton, &QPushButton::clicked, this, [this](){
                QListWidgetItem* item = roomList->currentItem();

                if(!item){
                    QMessageBox::information(this, "提示", "请先选择一个房间");
                    return;
                }

                /*
         * 当前暂时从列表文字中提取房间号。
         * 后面我们会给 QListWidgetItem 设置 UserRole，
         * 到时候这里会更规范。
         */
                bool ok = false;

                int roomId =
                    item->data(Qt::UserRole).toInt(&ok);

                if(!ok){
                    QMessageBox::warning(this, "错误", "无法获取房间ID");
                    return;
                }

                qDebug() << "准备加入房间：" << roomId;

                emit joinRoom(roomId);
            });


    // ================= 离开房间 =================
    connect(leaveButton, &QPushButton::clicked, this, [this](){
                emit leaveRoom();
            });


    // ================= 进入影院 =================
    connect(enterButton, &QPushButton::clicked, this, &RoomPage::enterCinema);


    // ================= 双击房间加入 =================
    connect(roomList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item){
        bool ok = false;

        int roomId = item->data(Qt::UserRole).toInt(&ok);

        if(!ok) return;

        qDebug() << "双击加入房间：" << roomId;

        emit joinRoom(roomId);
        });
}


void RoomPage::setUsername(const QString& username){
    usernameLabel->setText("用户：" + username);
}

void RoomPage::updateRoomList(const QStringList& rooms){
    roomList->clear();
    for(const QString& r: rooms){
        QStringList parts = r.split(' ');

        if(parts.isEmpty()) continue;

        bool ok = false;
        int roomId = parts[0].toInt(&ok);
        if(!ok) continue;

        auto* item = new QListWidgetItem(r);

        // 把真正的 room_id 存起来
        item->setData(Qt::UserRole, roomId);

        roomList->addItem(item);
    }
}

void RoomPage::updateRoomInfo(int roomId, const QString& roomName, int onlineCount){
    roomIdLabel->setText("房间：" + QString::number(roomId));
    roomNameLabel->setText("名称：" + roomName);
    onlineCountLabel->setText("在线人数：" + QString::number(onlineCount));
}

int RoomPage::currentRoomId() const{
    return m_currentRoomId;
}

void RoomPage::setCurrentRoomId(int roomId){
    m_currentRoomId = roomId;
}