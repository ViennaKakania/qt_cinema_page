#include "protocol.h"
#include <QTcpSocket>
#include <QDataStream>
#include <QByteArray>

// Qt 的 write() 自带发送缓冲队列，无需手写 send_all 循环
bool send_message(QTcpSocket* sock, MSG_type type, const string& msg){
    if (!sock || sock->state() != QAbstractSocket::ConnectedState) return false;

    QByteArray block;
    QDataStream out(&block, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::BigEndian); // 统一网络字节序，解决原代码跨平台隐患

    // 序列化包头 + 包体
    out << static_cast<quint32>(type) << static_cast<quint32>(msg.size());
    if (!msg.empty()) {
        out.writeRawData(msg.data(), static_cast<int>(msg.size()));
    }

    qint64 written = sock->write(block);
    // 如需严格阻塞等待“真正发完”，取消下行注释（勿在UI线程使用）
    // sock->waitForBytesWritten(-1);
    return (written == block.size());
}

// 保持原代码“同步阻塞读取”语义，上层 while/线程逻辑可零修改
bool recv_message(QTcpSocket* sock, Message& packet){
    if (!sock) return false;

    const int HEADER_SIZE = 8; // uint32_t * 2
    QByteArray headerBuf;

    // 1. 阻塞式读取包头
    while (headerBuf.size() < HEADER_SIZE) {
        if (sock->bytesAvailable() == 0) {
            // 等待数据到达，超时 3000ms（原 recv 是无限阻塞，加超时防死锁）
            if (!sock->waitForReadyRead(-1)) return false;
        }
        headerBuf.append(sock->read(HEADER_SIZE - headerBuf.size()));
    }

    // 2. 解析包头
    QDataStream in(headerBuf);
    in.setByteOrder(QDataStream::BigEndian);
    quint32 type, length;
    in >> type >> length;

    // 3. 阻塞式读取包体
    QByteArray bodyBuf;
    while (bodyBuf.size() < static_cast<int>(length)) {
        if (sock->bytesAvailable() == 0) {
            if (!sock->waitForReadyRead(-1)) return false;
        }
        bodyBuf.append(sock->read(static_cast<int>(length) - bodyBuf.size()));
    }

    // 4. 组装 Message
    packet.type = static_cast<MSG_type>(type);
    packet.data = string(bodyBuf.constData(), bodyBuf.size());
    return true;
}
