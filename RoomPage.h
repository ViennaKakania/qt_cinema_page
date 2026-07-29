#ifndef ROOMPAGE_H
#define ROOMPAGE_H

#include <QWidget>

class RoomPage: public QWidget{
    Q_OBJECT

public:
    explicit RoomPage(QWidget* parent = nullptr);

signals:
    void enterCinema();
};

#endif // ROOMPAGE_H
