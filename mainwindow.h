#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "networkclient.h"
#include <QTimer>

class QStackedWidget;
class LoginPage;
class RoomPage;
class CinemaPage;

class MainWindow: public QMainWindow
{
    Q_OBJECT
    QTimer* roomTimer;

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

signals:
    void sigChatReceived(const QString& msg);
    void sigPrivateReceived(const QString& msg);
    void sigSetNameReceived(const QString& msg);
    void sigExitReceived();
    void sigDisconnected();

private:
    NetworkClient* networkClient;
    QStackedWidget* stackedWidget;

    LoginPage* loginPage;
    RoomPage* roomPage;
    CinemaPage* cinemaPage;

private slots:
    void showRoomPage();
    void showCinemaPage();
    void showLoginPage();

    void onLoginRequested(const QString& username);
};

#endif // MAINWINDOW_H
