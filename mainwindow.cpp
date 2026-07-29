#include "mainwindow.h"

#include "loginpage.h"
#include "roompage.h"
#include "cinemapage.h"

#include <QStackedWidget>
#include <QFile>
#include <QDebug>

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

    networkClient->setOnChat([this](const string& msg){emit sigChatReceived(QString::fromStdString(msg));});
    networkClient->setOnPrivate([this](const string& msg){emit sigPrivateReceived(QString::fromStdString(msg));});
    networkClient->setOnSetName([this](const string& msg){emit sigSetNameReceived(QString::fromStdString(msg));});
    networkClient->setOnExit([this](){emit sigExitReceived();});
    networkClient->setOnDisconnect([this](){emit sigDisconnected();});

    connect(this, &MainWindow::sigChatReceived, this, [this](const QString &msg){
        qDebug() << "收到群聊消息:" << msg;
        // 在这里你可以把 msg 传给 roomPage 比如: roomPage->appendChatMsg(msg);
    });

    connect(this, &MainWindow::sigDisconnected, this, [this](){
        qDebug() << "与服务器断开连接";
        // 可以在这里加一个弹窗，或者切回到 LoginPage
        showLoginPage();
    });


    connect(loginPage, &LoginPage::loginSuccess, this, &MainWindow::showRoomPage);
    connect(roomPage, &RoomPage::enterCinema, this, &MainWindow::showCinemaPage);
    connect(cinemaPage, &CinemaPage::backToRoom, this, &MainWindow::showRoomPage);


    if (networkClient->connect("192.168.145.129", 9090)){
        qDebug() << "服务器连接成功";
    }
    else{
        qDebug() << "服务器连接失败，请检查网络或服务端";
    }
}

void MainWindow::showRoomPage(){
    stackedWidget->setCurrentWidget(roomPage);
}
void MainWindow::showCinemaPage(){
    stackedWidget->setCurrentWidget(cinemaPage);
}
void MainWindow::showLoginPage(){
    stackedWidget->setCurrentWidget(loginPage);
}

MainWindow::~MainWindow(){
}