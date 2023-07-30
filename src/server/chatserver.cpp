#include "chatserver.hpp"
#include "json.hpp"
#include "chatservice.hpp"

#include <iostream>
#include <functional> //函数对象绑定器
#include <string>
using namespace std;
using namespace placeholders; // 参数占位符
using json = nlohmann::json;

// 初始化聊天服务器对象
ChatServer::ChatServer(EventLoop *loop,
                       const InetAddress &listenAddr,
                       const string &nameArg)
    : _server(loop, listenAddr, nameArg), _loop(loop)
{
    // 注册链接回调
    _server.setConnectionCallback(std::bind(&ChatServer::onConnection, this, _1));

    // 注册消息回调
    _server.setMessageCallback(std::bind(&ChatServer::onMessage, this, _1, _2, _3));

    // 设置线程数量
    _server.setThreadNum(4);
}

// 启动服务
void ChatServer::start()
{
    _server.start();
}
/*
回调函数：
    CSDN：关于C++ 回调函数(callback) 精简且实用
*/

// 上报连接相关信息的回调函数
void ChatServer::onConnection(const TcpConnectionPtr &conn)
{
    // 客户端断开链接
    if (!conn->connected()) // 连接失败
    {
        ChatService::instance()->clientCloseException(conn); // 处理客户端异常关闭
        conn->shutdown();
    }
}

// 上报读写事件相关信息的回调函数
void ChatServer::onMessage(const TcpConnectionPtr &conn,
                           Buffer *buffer,
                           Timestamp time)
{
    // 接收缓冲区中的数据
    string buf = buffer->retrieveAllAsString();

    // 测试，添加json打印代码
    // cout << buf << endl;

    // 数据的反序列化
    json js = json::parse(buf);

    // 达到的目的：完全解耦网络模块的代码和业务模块的代码

    // 通过js["msgid"] 获取->业务handler->conn  js  time
    auto msgHandler = ChatService::instance()->getHandler(js["msgid"].get<int>());
    // 回调消息绑定好的事件处理器，来执行相应的业务处理
    msgHandler(conn, js, time);
}