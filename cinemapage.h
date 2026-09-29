#ifndef CINEMAPAGE_H
#define CINEMAPAGE_H

#include "FFmpegPlayer.h"
class FFmpegDecodeThread;

#include <QWidget>
#include <QTimer>

#include <string>
using std::string;

class QPushButton;
class QLabel;
class QListWidget;
class QLineEdit;
class QSlider;

class CinemaPage: public QWidget{
    Q_OBJECT

public:
    explicit CinemaPage(QWidget* parent = nullptr);

    ~CinemaPage();

    void addChatMessage(const QString& message);

signals:
    void backToRoom();
    void sendChatMsg(const QString& message);

private:
    void displayNextFrame();  // 显示帧

private:
    QPushButton* backButton;
    QPushButton* playButton;
    QPushButton* sendButton;

    QLabel* timeLabel;

    QListWidget* chatList;
    QLineEdit* messageEdit;
    QSlider* progressSlider;

    FFmpegPlayer* player;
    FFmpegDecodeThread* decodeThread;
    QLabel* videoLabel;

    QTimer* playbackTimer; // 视频
    QTimer* progressTimer; // UI


    bool playing;
    bool seeking;
};



#endif // CINEMAPAGE_H
