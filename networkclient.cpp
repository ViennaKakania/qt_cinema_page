#include "networkclient.h"
#include <QTcpSocket>
#include <QHostAddress>


NetworkClient::NetworkClient(): m_sock(nullptr), m_connected(false) {}

NetworkClient::~NetworkClient(){
    disconnect();
}

bool NetworkClient::connect(const string& ip, int port){
    // ===== 变动：替换 socket() + ::connect() =====
    m_sock = new QTcpSocket();  // 无 parent，避免线程归属问题

    m_sock->connectToHost(
        QHostAddress(QString::fromStdString(ip)),
        static_cast<quint16>(port)
        );

    // 阻塞等待连接建立（等价于原来的同步 connect）
    if (!m_sock->waitForConnected(5000)) {
        delete m_sock;
        m_sock = nullptr;
        return false;
    }
    // ================================================

    m_connected = true;
    m_recvThread = std::thread(&NetworkClient::recvLoop, this);
    return true;
}

void NetworkClient::disconnect(){
    m_connected = false;

    // ===== 变动：替换 shutdown + close =====
    if (m_sock) {
        m_sock->disconnectFromHost();
        if (m_sock->state() != QAbstractSocket::UnconnectedState) {
            m_sock->waitForDisconnected(1000);
        }
        delete m_sock;
        m_sock = nullptr;
    }
    // ================================================

    if (m_recvThread.joinable()) {
        m_recvThread.join();
    }
}

bool NetworkClient::isConnected() const{
    return m_connected;
}

// ===== 变动：加锁保护（recvLoop 线程也在读 socket）=====
void NetworkClient::sendChat(const string& msg){
    std::lock_guard<std::mutex> lock(m_sendMutex);
    send_message(m_sock, MSG_chat, msg);
}

void NetworkClient::sendPrivate(const string& msg){
    std::lock_guard<std::mutex> lock(m_sendMutex);
    send_message(m_sock, MSG_private, msg);
}

void NetworkClient::sendSetName(const string& name){
    std::lock_guard<std::mutex> lock(m_sendMutex);
    send_message(m_sock, MSG_set_name, name);
}

void NetworkClient::sendExit(){
    std::lock_guard<std::mutex> lock(m_sendMutex);
    send_message(m_sock, MSG_exit, "");
}
// ========================================================

void NetworkClient::setOnChat(function<void(const string&)> cb) { m_onChat = cb; }
void NetworkClient::setOnPrivate(function<void(const string&)> cb) { m_onPrivate = cb; }
void NetworkClient::setOnSetName(function<void(const string&)> cb) { m_onSetName = cb; }
void NetworkClient::setOnExit(function<void()> cb) { m_onExit = cb; }
void NetworkClient::setOnDisconnect(function<void()> cb) { m_onDisconnect = cb; }

// recvLoop 逻辑完全不变，只是底层 recv_message 已换成 Qt 实现
void NetworkClient::recvLoop(){
    while(m_connected){
        Message msg;
        if(!recv_message(m_sock, msg)){
            m_connected = false;
            if (m_onDisconnect) m_onDisconnect();
            break;
        }

        switch(msg.type){
        case MSG_chat:
            if(m_onChat) m_onChat(msg.data);
            break;
        case MSG_private:
            if(m_onPrivate) m_onPrivate(msg.data);
            break;
        case MSG_set_name:
            if(m_onSetName) m_onSetName(msg.data);
            break;
        case MSG_exit:
            if(m_onExit) m_onExit();
            break;
        }
    }
}




