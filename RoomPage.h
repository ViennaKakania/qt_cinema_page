#ifndef ROOMPAGE_H
#define ROOMPAGE_H

#include <QWidget>
#include <QString>
#include <QStringList>

class QLabel;
class QPushButton;
class QListWidget;
class QListWidgetItem;
class QLineEdit;
class QListWidget;

class RoomPage: public QWidget{
    Q_OBJECT

public:
    explicit RoomPage(QWidget* parent = nullptr);

    // 更新当前用户名
    void setUsername(const QString& username);

    // 更新房间列表
    void updateRoomList(const QStringList& rooms);

    // 更新当前房间信息
    void updateRoomInfo(int roomId, const QString& roomName, int onlineCount);

    // 返回当前房间id
    int currentRoomId() const;

    // 设置页面上的当前房间
    void setCurrentRoomId(int roomId);

signals:
    void enterCinema();

    // 创建房间
    void createRoom(int roomId, const QString& roomName);

    // 加入房间
    void joinRoom(int roomId);

    // 离开房间
    void leaveRoom();

    // 请求刷新房间列表
    void requestRoomList();

private:
    QLabel* usernameLabel;

    QListWidget* roomList;

    QLabel* roomIdLabel;
    QLabel* roomNameLabel;
    // QLabel* roomOwnerLabel; 暂时不做房主
    QLabel* onlineCountLabel;

    QLineEdit* roomIdEdit;
    QLineEdit* roomNameEdit;

    QPushButton* createButton;
    QPushButton* joinButton;
    QPushButton* leaveButton;
    QPushButton* enterButton;

    int m_currentRoomId = 1001;
};



#endif // ROOMPAGE_H
