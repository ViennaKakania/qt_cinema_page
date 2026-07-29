#ifndef CINEMAPAGE_H
#define CINEMAPAGE_H

#include <QWidget>

class QPushButton;
class QLabel;
class QListWidget;
class QLineEdit;
class QSlider;

class CinemaPage: public QWidget{
    Q_OBJECT

public:
    explicit CinemaPage(QWidget* parent = nullptr);

signals:
    void backToRoom();

private:
    QPushButton* backButton;
    QPushButton* playButton;
    QPushButton* sendButton;

    QLabel* roomInfo;
    QLabel* timeLabel;

    QListWidget* chatList;
    QLineEdit* messageEdit;
    QSlider* progressSlider;
};



#endif // CINEMAPAGE_H
