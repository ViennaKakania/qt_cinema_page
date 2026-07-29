#ifndef NETWORKCLIENT_H
#define NETWORKCLIENT_H

#include <string>
#include <thread>
#include <mutex>
#include <functional>
#include "protocol.h"

class QTcpSocket;
using std::string;
using std::thread;
using std::mutex;
using std::function;

class NetworkClient {
public:
    NetworkClient();
    ~NetworkClient();

    bool connect(const string& ip, int port);
    void disconnect();
    bool isConnected() const;

    void sendChat(const string& msg);
    void sendPrivate(const string& msg);
    void sendSetName(const string& name);
    void sendExit();

    void setOnChat(function<void(const string&)> cb);
    void setOnPrivate(function<void(const string&)> cb);
    void setOnSetName(function<void(const string&)> cb);
    void setOnExit(function<void()> cb);
    void setOnDisconnect(function<void()> cb);

private:
    void recvLoop();

    QTcpSocket* m_sock;

    bool m_connected;
    thread m_recvThread;
    mutex m_sendMutex;  // 新增：保护多线程 write

    function<void(const string&)> m_onChat;
    function<void(const string&)> m_onPrivate;
    function<void(const string&)> m_onSetName;
    function<void()> m_onExit;
    function<void()> m_onDisconnect;
};



#endif // NETWORKCLIENT_H
