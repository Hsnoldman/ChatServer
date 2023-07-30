/*
muduo网络库给用户提供两个主要的类
TcpServer: 用于编写服务器程序
TcpClient: 用于编写客户端程序

epoll+线程池
优点：能够把网络I/O的代码和业务代码区分开    业务代码：用户的连接和断开 用户的读写事件
*/

#include <iostream>
#include <muduo/net/TcpClient.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/TcpServer.h>
#include <functional>
#include <string>
using namespace muduo;
using namespace muduo::net;
using namespace std;

/*
基于muduo网络库开发服务器程序
1.组合TcpServer对象
2.创建EventLoop事件循环对象的指针
3.明确TcpServer构造函数需要什么参数，输出ChatServer的构造函数
4.在当前服务器类的构造函数中，注册处理连接的回调函数和处理读写事件的回调函数
5.设置合适的服务端线程数量，muduo库会自动分配I/O线程和工作线程
*/
class ChatServer
{
public:
    // 初始化TcpServer
    ChatServer(EventLoop *loop,               // 事件循环
               const InetAddress &listenAddr, // IP + Port
               const string &nameArg          // 服务器的名字
               ) : _server(loop, listenAddr, "ChatServer")
    {
        // 通过绑定器设置回调函数，给服务器注册用户连接的创建和断开回调
        _server.setConnectionCallback(bind(&ChatServer::onConnection, this, _1));
        // 给服务器注册读写事件的回调
        _server.setMessageCallback(bind(&ChatServer::onMessage, this, _1, _2, _3)); //_1 _2 _3参数占位符
        // 设置EventLoop的线程数量
        _server.setThreadNum(6);
    }
    // 启动ChatServer服务
    void start()
    {
        _server.start();
    }

private:
    // TcpServer绑定的回调函数，当有新连接或连接中断时调用，专门处理用户的连接创建和断开 epoll listenfd accept
    void onConnection(const TcpConnectionPtr &conn);
    // TcpServer绑定的回调函数，专门处理用户的读写事件
    void onMessage(const TcpConnectionPtr &conn, // 连接
                   Buffer *buffer,               // 缓冲区
                   Timestamp time);              // 接收数据的时间信息

private:
    TcpServer _server; // #1 定义TcpServer对象
    EventLoop *_loop;  // #2 epoll EventLoop事件循环对象的指针
};

void ChatServer::onConnection(const TcpConnectionPtr &conn)
{
    if (conn->connected())
    {
        cout << conn->peerAddress().toIpPort() << " -> " << conn->localAddress().toIpPort() << " state::online" << endl;
    }
    else
    {
        cout << conn->peerAddress().toIpPort() << " -> " << conn->localAddress().toIpPort() << " state::offline" << endl;
        conn->shutdown(); // close(fd)
        // _loop->quit();
    }
}

void ChatServer::onMessage(const TcpConnectionPtr &conn, Buffer *buffer, Timestamp time)
{
    // string buf = buffer->retrieveAllAsString();
    string buf = "server: ";
    buf.append(buffer->retrieveAllAsString());
    cout << "recieve data: " << buf << "time: " << time.toFormattedString() << endl;
    
    conn->send(buf);
}

int main()
{
    EventLoop loop; // epoll
    InetAddress addr("192.168.35.129", 6000);
    ChatServer server(&loop, addr, "ChatServer");

    server.start(); // listenfd epoll_ctl -> epoll
    loop.loop();    // epoll_wait 阻塞的方式等待新用户的连接，已连接用户的读写事件等

    return 0;
}

// g++ -o muduo_server muduo_server.cpp -lmuduo_net -lmuduo_base -lpthread