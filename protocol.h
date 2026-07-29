#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>
#include <string>

// 前向声明，避免头文件包含过重
class QTcpSocket;

using std::string;

enum MSG_type
{
    MSG_chat = 1,      // 群聊
    MSG_private,       // 私聊
    MSG_set_name,
    MSG_exit
};

// 保留原结构体（上层逻辑可能用到），但网络传输不再直接 memcpy 它
struct Header{
    uint32_t type;
    uint32_t length;
};

struct Message{
    MSG_type type;
    string data; // 最小变动：保留 std::string。如需更 Qt 风格可后续改为 QString
};

// 变动：int sock -> QTcpSocket* sock
// send_all / recv_all 在 Qt 中已被内部缓冲机制替代，若上层未单独调用可删除
bool send_message(QTcpSocket* sock, MSG_type type, const string& msg);
bool recv_message(QTcpSocket* sock, Message& packet);

#endif // PROTOCOL_H