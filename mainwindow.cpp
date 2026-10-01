#include "mainwindow.h"

#include "loginpage.h"
#include "roompage.h"
#include "cinemapage.h"

#include <QStackedWidget>
#include <QFile>
#include <QDebug>
#include <QMessageBox>
#include <QMetaObject>
#include <QLabel>
#include <QTimer>

MainWindow::MainWindow(QWidget* parent): QMainWindow(parent){
    stackedWidget = new QStackedWidget(this);
    networkClient = new NetworkClient();

    loginPage = new LoginPage;
    roomPage = new RoomPage;
    cinemaPage = new CinemaPage;

    stackedWidget->addWidget(loginPage);
    stackedWidget->addWidget(roomPage);
    stackedWidget->addWidget(cinemaPage);

    setCentralWidget(stackedWidget);
    stackedWidget->setCurrentWidget(loginPage);

    QFile file(":/new/prefix1/style.qss");
    if(file.open(QFile::ReadOnly | QFile::Text)){
        setStyleSheet(file.readAll());
    }

    // ================= 接收服务器 MSG_chat =================
    networkClient->setOnChat([this](const string& msg){
        QString message = QString::fromStdString(msg);

        // 其他服务器消息
        qDebug() << "Server:" << message;

        // 房间列表
        if(message.startsWith("========= 房间列表 =========")){
            QStringList rooms;

            QStringList lines = message.split('\n');

            for(const QString& line: lines){
                QString text = line.trimmed();

                if(text.isEmpty()) continue;

                if(text.startsWith("=========")) continue;

                rooms.append(text);
            }

            // 根据当前房间ID更新房间信息
            QMetaObject::invokeMethod(roomPage, [this, rooms](){
                    roomPage->updateRoomList(rooms);

                    const int currentId = roomPage->currentRoomId();

                    for(const QString& room : rooms){
                        QStringList parts = room.split(' ', Qt::SkipEmptyParts);

                        if(parts.size() < 2) continue;

                        bool ok = false;
                        int roomId = parts[0].toInt(&ok);
                        if(!ok || roomId != currentId) continue;

                        QString roomInfo = parts.mid(1).join(' ');

                        int left = roomInfo.lastIndexOf(QString::fromUtf8("（"));
                        int right = roomInfo.lastIndexOf(QString::fromUtf8("）"));

                        if(left == -1 || right == -1 || right <= left) continue;

                        QString roomName = roomInfo.left(left).trimmed();
                        QString countText = roomInfo.mid(left+1, right-left-1);

                        countText.remove(QString::fromUtf8("人"));

                        bool countOk = false;
                        int onlineCount = countText.toInt(&countOk);

                        if(countOk){
                            roomPage->updateRoomInfo(roomId, roomName, onlineCount);
                        }
                        break;
                    }
                },
                Qt::QueuedConnection
                );

            return;
        }

        // 创建房间成功
        if(message == "创建成功"){

            qDebug() << "房间创建成功，重新获取房间列表";

            networkClient->sendChat("/rooms");

            return;
        }

        // 加入房间成功
        if(message == "加入成功"){
            qDebug() << "加入房间成功，重新获取房间列表";

            networkClient->sendChat("/rooms");

            return;
        }

        // 离开房间成功
        if(message == "已返回大厅"){
            qDebug() << "离开房间成功，重新获取房间列表";

            roomPage->setCurrentRoomId(1001);

            networkClient->sendChat("/rooms");

            return;
        }

        // 普通聊天消息
        // 服务器已经保证消息只发送给同一房间用户，
        // Qt客户端只负责显示。
        QMetaObject::invokeMethod(cinemaPage, [this, message](){ cinemaPage->addChatMessage(message);}, Qt::QueuedConnection);

    });

    networkClient->setOnPrivate([this](const string& msg){emit sigPrivateReceived(QString::fromStdString(msg));});
    networkClient->setOnSetName([this](const string& msg){emit sigSetNameReceived(QString::fromStdString(msg));});
    networkClient->setOnExit([this](){emit sigExitReceived();});
    networkClient->setOnDisconnect([this](){emit sigDisconnected();});


    connect(loginPage, &LoginPage::sigLoginRequested, this, &MainWindow::onLoginRequested);

    connect(roomPage, &RoomPage::enterCinema, this, &MainWindow::showCinemaPage);

    connect(roomPage, &RoomPage::createRoom, this, [this](int roomId, const QString& roomName){
                QString command = "/create " + QString::number(roomId) + " " + roomName;

                networkClient->sendChat(command.toStdString());
            });  // 创建房间

    connect(roomPage, &RoomPage::joinRoom, this, [this](int roomId){
                roomPage->setCurrentRoomId(roomId); // 先记录当前房间

                QString command = "/join " + QString::number(roomId);

                networkClient->sendChat(command.toStdString());
            });  // 加入房间

    connect(roomPage, &RoomPage::leaveRoom, this, [this](){
        networkClient->sendChat("/leave");

        // 暂时回到大厅
        roomPage->setCurrentRoomId(1001);

        // 重新获取房间列表
        networkClient->sendChat("/rooms");
        });  //离开房间

    connect(roomPage, &RoomPage::requestRoomList, this, [this](){networkClient->sendChat("/rooms"); });  // 刷新房间列表

    connect(cinemaPage, &CinemaPage::backToRoom, this, &MainWindow::showRoomPage); // 返回房间页面

    connect(cinemaPage, &CinemaPage::sendChatMsg, this, [this](const QString& message){networkClient->sendChat(message.toStdString());}); // CinemaPage发送聊天消息

    // 实时刷新房间信息
    // 每3秒Qt发送 /rooms ，服务器返回房间列表，MainWindow解析，RoomPage更新
    roomTimer = new QTimer(this);
    connect(roomTimer, &QTimer::timeout, this, [this](){
        if(networkClient->isConnected()){
            networkClient->sendChat("/rooms");
        }
    });
    roomTimer->start(3000);
}

MainWindow::~MainWindow(){
}

void MainWindow::showRoomPage(){
    stackedWidget->setCurrentWidget(roomPage);
}
void MainWindow::showCinemaPage(){
    // 首次进入时打开片源；后续进入保留当前播放状态
    if(!cinemaPage->isVideoReady()){
        cinemaPage->openVideo(QString());     // 空 → 弹文件选择框
    }
    stackedWidget->setCurrentWidget(cinemaPage);

}
void MainWindow::showLoginPage(){
    stackedWidget->setCurrentWidget(loginPage);
}

void MainWindow::onLoginRequested(const QString& username){
    // 1. 尝试连接服务器
    if(networkClient->connect("192.168.145.129", 9090)){
        // 2. 完美契合服务端要求：连接成功后，第一件事就是立刻发送 MSG_set_name 协议包
        networkClient->sendSetName(username.toStdString());  // 连接成功后先设置用户名

        // 3. 切换到房间页面
        showRoomPage();

        // 登录成功后立即获取房间列表
        networkClient->sendChat("/rooms");

        qDebug() << "连接成功";
    }
    else{
        // 连接失败的界面提示
        QMessageBox::critical(this, "连接失败", "无法连接到服务器，请检查网络！");
    }
}
