#include "loginpage.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpacerItem>

LoginPage::LoginPage(QWidget *parent): QWidget(parent){
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(200, 100, 200, 50);
    mainLayout->setSpacing(15);

    auto* title = new QLabel("异地同步影院");
    auto* subtitle = new QLabel("Long Distance Cinema");

    auto* usernameEdit = new QLineEdit;
    auto* passwordEdit = new QLineEdit;
    auto* loginButton = new QPushButton("登录");

    title->setAlignment(Qt::AlignCenter);
    subtitle->setAlignment(Qt::AlignCenter);

    usernameEdit->setPlaceholderText("请输入用户名");
    passwordEdit->setPlaceholderText("请输入密码");
    passwordEdit->setEchoMode(QLineEdit::Password);

    usernameEdit->setFixedHeight(40);
    passwordEdit->setFixedHeight(40);
    loginButton->setFixedHeight(40);

    mainLayout->addStretch();

    mainLayout->addWidget(title);
    mainLayout->addWidget(subtitle);
    mainLayout->addSpacing(30);
    mainLayout->addWidget(usernameEdit);
    mainLayout->addWidget(passwordEdit);
    mainLayout->addSpacing(20);
    mainLayout->addWidget(loginButton);

    mainLayout->addStretch();



    auto slot = [this](){
        emit loginSuccess();
    };
    connect(loginButton, &QPushButton::clicked, this, slot);
}