#ifndef LOGINPAGE_H
#define LOGINPAGE_H

#include <QWidget>
class QLineEdit;
class QPushButton;

class LoginPage : public QWidget
{
    Q_OBJECT

public:
    explicit LoginPage(QWidget* parent = nullptr);

signals:
    void sigLoginRequested(const QString& username);

private:
    QLineEdit* nameLineEdit;
    QPushButton* loginBtn;
};

#endif // LOGINPAGE_H
