### 项目环境

1.unbantu linux环境

2.json库，轻量级的数据交换语言

3.boost库+muduo网络库

4.Redis，内存高速缓存数据库

5.MySQL数据库

6.Nginx,高性能的HTTP和反向代理服务器

7.CMake构建项目

### 项目编译
清空/bin和/build目录，进入/build目录执行 cmake.. make

### 项目启动

#### 启动Redis服务

配置后台启动

##### **1.配置redis.conf**

修改Redis安装目录下的配置文件redis.conf，将其中的daemonize由no改为yes

```
daemonize yes
```

##### 2.启动redis

切换至Redis安装目录下，执行下面指令

```
src/redis-server redis.conf
```

也可直接切换至Redis安装目录下的src目录下运行，执行下面指令

```
./redis-server ../redis.conf
```

#### 启动nginx服务

##### 1.进入nginx/sbin目录

```shell
 cd /usr/local/nginx/sbin/  
```

##### 2.启动nginx

```shell
./nginx 
```

#####  3.nginx配置文件目录

```shell
    cd /usr/local/nginx/conf/nginx.conf //配置文件路径
```

##### 4.nginx.conf文件配置

```c
  stream {
  	upstream MyServer {
   		server 127.0.0.1:6000 weight=1 max_fails=3 fail_timeout=30s;
   		server 127.0.0.1:6001 weight=1 max_fails=3 fail_timeout=30s;
   		server 127.0.0.1:6002 weight=1 max_fails=3 fail_timeout=30s;
    	}
  	server {
  		proxy_connect_timeout 1s;
   		listen 8000;
		proxy_pass MyServer;
    	tcp_nodelay on;
  		}
}
```

相关参数：**max_fail	fail_timeout**

这个是Nginx在负载均衡功能中，用于判断后端节点状态所用到两个参数。Nginx基于连接探测，如果发现后端异常，在单位周期为fail_timeout设置的时间，中达到max_fails次数，这个周期次数内，如果后端同一个节点不可用，那么接将把节点标记为不可用，并等待下一个周期（同样时常为fail_timeout）再一次去请求，判断是否连接是否成功，即心跳机制。

默认情况下，**fail_timeout=10s，max_fails=1**

这两个参数如果调大，Nginx相当于把请求缓冲，如果整体的的后端服务处于可用状态，对于高并发的场景来说，建议适当调大是有效的。

##### 5.配置完成后重启nginx服务

```shell
nginx -s reload //重新加载配置文件启动
```

#### 启动服务端ChatServer

在nginx.conf中配置了三台服务器进行负载均衡

```shell
./ChatServer 127.0.0.1 6000
./ChatServer 127.0.0.1 6001
./ChatServer 127.0.0.1 6002
```

####  启动客户端ChatClient

```shell
./ChatClient 127.0.0.1 8000
```



### 数据库表设计

#### User表

| 字段名称 | 字段类型                 | 字段说明     | 约束                        |
| -------- | :----------------------- | ------------ | --------------------------- |
| id       | INT                      | 用户id       | PRIMARY KEY、AUTO_INCREMENT |
| name     | VARCHAR(50)              | 用户名       | NOT NULL, UNIQUE            |
| password | VARCHAR(50)              | 登陆密码     | NOT NULL                    |
| state    | ENUM('online','offline') | 当前登陆状态 | DEFAULT 'offline'           |

#### Friend表

| 字段名称 | 字段类型 | 字段说明 | 约束               |
| -------- | -------- | -------- | ------------------ |
| userid   | INT      | 用户id   | NOT NULL、联合主键 |
| friendid | INT      | 好友id   | NOT NULL、联合主键 |

#### AllGroup表

| 字段名称  | 字段类型     | 字段说明   | 约束                        |
| --------- | ------------ | ---------- | --------------------------- |
| id        | INT          | 组id       | PRIMARY KEY、AUTO_INCREMENT |
| groupname | VARCHAR(50)  | 组名称     | NOT NULL,UNIQUE             |
| groupdesc | VARCHAR(200) | 组功能描述 | DEFAULT ' '                 |

#### GroupUser表

| 字段名称  | 字段类型                 | 字段说明 | 约束               |
| --------- | ------------------------ | -------- | ------------------ |
| groupid   | INT                      | 组id     | NOT NULL、联合主键 |
| userid    | INT                      | 组员id   | NOT NULL、联合主键 |
| grouprole | ENUM('creator','normal') | 组内角色 | DEFAULT 'normal'   |

#### OfflineMessage表

| 字段名称 | 字段类型     | 字段说明         | 约束     |
| -------- | ------------ | ---------------- | -------- |
| userid   | INT          | 用户id           | NOT NULL |
| message  | VARCHAR(500) | 离线消息（json） | NOT NULL |

### ChatServer服务端

#### 1.ChatServer类

聊天服务器的主类，使用muduo网络库

主要功能：

- 创建一个server服务器相关对象

```c++
 	TcpServer _server; // 组合的muduo库，实现服务器功能的类对象
 	EventLoop *_loop;  // 指向事件循环对象的指针
```

- 注册相应的回调函数，设置线程数量

```c++
    // 注册链接回调
    _server.setConnectionCallback(std::bind(&ChatServer::onConnection, this, _1));
    // 注册消息回调
    _server.setMessageCallback(std::bind(&ChatServer::onMessage, this, _1, _2, _3));
    // 设置线程数量
    _server.setThreadNum(4);
```

- onConnection函数

处理客户端异常关闭，调用shutdown()关闭这条连接

```c++
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
```

- onMessage函数

当服务器接收到一条新消息时：

​	将接收的数据(json)反序列化

​	通过ChatService::instance()->getHandler()获取该消息对应的回调函数

​	执行回调函数,将这条请求的json与它对应的TcpConnection连接作为参数传入

```c++
// 上报读写事件相关信息的回调函数
void ChatServer::onMessage(const TcpConnectionPtr &conn, Buffer *buffer, Timestamp time)
{
    string buf = buffer->retrieveAllAsString(); // 接收缓冲区中的数据
    json js = json::parse(buf); // 数据的反序列化

    // 达到的目的：完全解耦网络模块的代码和业务模块的代码
    // 通过js["msgid"] 获取->业务handler->conn  js  time
    auto msgHandler = ChatService::instance()->getHandler(js["msgid"].get<int>());
    // 回调消息绑定好的事件处理器，来执行相应的业务处理
    msgHandler(conn, js, time);
}
```

#### 2.ChatService类

聊天服务器业务类，定义了主要的业务逻辑

**主要成员变量**

```c++
    // 单例模式，构造函数私有化
    ChatService();

	using MsgHandler = std::function<void(const TcpConnectionPtr &conn, json &js, Timestamp)>;
    // 存储消息id和其对应的业务处理方法
    unordered_map<int, MsgHandler> _msgHandlerMap;

    // 存储在线用户的通信连接
    unordered_map<int, TcpConnectionPtr> _userConnMap;
    // 定义互斥锁，保证_userConnMap的线程安全
    mutex _connMutex;

    // 数据操作类对象
    UserModel _userModel;             // 用户数据
    OfflineMsgModel _offlineMsgModel; // 用户离线消息数据
    FriendModel _friendModel;         // 好友信息数据
    GroupModel _groupModel;           // 群组数据

    // redis操作对象
    Redis _redis;
```

- MsgHandler的作用

对应一条消息id绑定的事件，承载对应的回调函数。

- _msgHandlerMap对象

hash表结构，存储所有类型的消息和它对应的业务处理函数 key为业务id，value为对应的业务处理函数

**主要成员函数**

- ChatService构造函数

ChatService的构造函数为私有函数，在类外进行初实现

在构造函数中将业务对应的处理函数插入到_msgHandlerMap表中，连接redis服务器，注册redis上报消息的回调

- 获取单例对象接口的函数

```c++
static ChatService *instance();
```

- 业务处理函数

```c++
   // 处理登录业务
    void login(const TcpConnectionPtr &conn, json &js, Timestamp time);
    // 处理注册业务
    void reg(const TcpConnectionPtr &conn, json &js, Timestamp time);
    // 一对一聊天业务
    void oneChat(const TcpConnectionPtr &conn, json &js, Timestamp time);
    // 添加好友业务
    void addFriend(const TcpConnectionPtr &conn, json &js, Timestamp time);
    // 创建群组业务
    void createGroup(const TcpConnectionPtr &conn, json &js, Timestamp time);
    // 加入群组业务
    void addGroup(const TcpConnectionPtr &conn, json &js, Timestamp time);
    // 群组聊天业务
    void groupChat(const TcpConnectionPtr &conn, json &js, Timestamp time);
    // 处理注销业务
    void loginout(const TcpConnectionPtr &conn, json &js, Timestamp time);
```

- 获取消息对应的业务处理函数

```c++
MsgHandler getHandler(int msgid); //参数为消息id
```

- 由ChatServer对象调用

```c++
auto msgHandler = ChatService::instance()->getHandler(js["msgid"].get<int>());
```

- 从redis消息队列中获取订阅的消息

```c++
void handleRedisSubscribeMessage(int, string);
```

- 异常处理函数

```c++
 	// 处理客户端异常退出
    void clientCloseException(const TcpConnectionPtr &conn);
    // 服务器异常，业务重置方法
    void reset();
```

#### 3.public类

枚举类型，定义了不同业务的消息id，作为公共文件，在对应的文件中引入来识别对应的消息

```c++
enum EnMsgType
{
    LOGIN_MSG = 1,  // 登录消息
    LOGIN_MSG_ACK = 2,  // 登录响应消息
    LOGINOUT_MSG = 3,   // 注销消息
    REG_MSG = 4,        // 注册消息
    REG_MSG_ACK = 5,    // 注册响应消息
    ONE_CHAT_MSG = 6,   // 一对一聊天消息
    ADD_FRIEND_MSG = 7, // 添加好友消息

    CREATE_GROUP_MSG = 8, // 创建群组
    ADD_GROUP_MSG = 9,    // 加入群组
    GROUP_CHAT_MSG = 10,   // 群聊天
};
```

#### 4.MYSQL类

数据库操作类，执行具体的数据库操作

```c++
#include <mysql/mysql.h>
// 数据库操作类
class MySQL
{
public:
    // 初始化数据库连接
    MySQL();
    // 释放数据库连接资源
    ~MySQL();
    // 连接数据库
    bool connect();
    // 更新操作
    bool update(string sql);
    // 查询操作
    MYSQL_RES *query(string sql);
    // 获取连接
    MYSQL* getConnection();
private:
    MYSQL *_conn;
};
```

#### 5.Redis类

定义了Redis相关操作

```c++
#include <hiredis/hiredis.h>
class Redis
{
public:
    Redis();
    ~Redis();
    // 连接redis服务器 
    bool connect();
    // 向redis指定的通道channel发布消息
    bool publish(int channel, string message);
    // 向redis指定的通道subscribe订阅消息
    bool subscribe(int channel);
    // 向redis指定的通道unsubscribe取消订阅消息
    bool unsubscribe(int channel);
    // 在独立线程中接收订阅通道中的消息
    void observer_channel_message();
    // 初始化向业务层上报通道消息的回调对象
    void init_notify_handler(function<void(int, string)> fn);

private:
    redisContext *_publish_context; // hiredis同步上下文对象，负责publish消息
    redisContext *_subcribe_context; // hiredis同步上下文对象，负责subscribe消息
    function<void(int, string)> _notify_message_handler; // 回调操作，收到订阅的消息，给service层上报
};
```

#### 6.User类

负责存储一个用户的相关数据，并提供了设置和获取相应数据的成员方法

```c++
class User
{
public:
    User(int id = -1, string name = "", string password = "", string state = "offline")
    {
        this->id = id;
        this->name = name;
        this->password = password;
        this->state = state;
    }

    void setId(int id) { this->id = id; }
    void setName(string name) { this->name = name; }
    void setPwd(string password) { this->password = password; }
    void setState(string state) { this->state = state; }

    int getId() { return this->id; }
    string getName() { return this->name; }
    string getPwd() { return this->password; }
    string getState() { return this->state; }

protected:
    int id;          // 用户id
    string name;     // 用户名
    string password; // 登陆密码
    string state;    // 登陆状态
};
```

#### 7.UserModel类

user表的数据操作类

定义了相应用户相关业务的操作接口方法

```c++
#include "user.hpp"
class UserModel {
public:
    bool insert(User &user); // User表的增加方法

    User query(int id); // 根据用户号码查询用户信息
  
    bool updateState(User user); // 更新用户的状态信息

    void resetState(); // 重置用户的状态信息
};
```

#### 8.Group类

负责存储一个群组的相关数据，并提供了设置和获取相应数据的成员方法

```c++
#include "groupuser.hpp"
class Group
{
public:
    Group(int id = -1, string name = "", string desc = "")
    {
        this->id = id;
        this->name = name;
        this->desc = desc;
    }

    void setId(int id) { this->id = id; }
    void setName(string name) { this->name = name; }
    void setDesc(string desc) { this->desc = desc; }

    int getId() { return this->id; }
    string getName() { return this->name; }
    string getDesc() { return this->desc; }
    vector<GroupUser> &getUsers() { return this->users; }

private:
    int id; //群组id
    string name; //群组名
    string desc; //群组描述
    vector<GroupUser> users; //存储群组内的所有用户
};
```

#### 9.GroupUser类

继承自User类，提供一个群组角色的成员变量

```c++
#include "user.hpp"
class GroupUser : public User
{
public:
    // 设置成员角色
    void setRole(string role) { this->role = role; }
    // 获取成员角色
    string getRole() { return this->role; }
private:
    string role; // 成员角色(creator/normal)
};
```

#### 10.GroupModel类

定义了群组相关业务的操作接口方法

```c++
#include "group.hpp"
class GroupModel
{
public:
    // 创建群组
    bool createGroup(Group &group);
    // 加入群组
    void addGroup(int userid, int groupid, string role);
    // 查询用户所在群组信息
    vector<Group> queryGroups(int userid);
    // 根据指定的groupid查询群组用户id列表，除userid自己，主要用户群聊业务给群组其它成员群发消息
    vector<int> queryGroupUsers(int userid, int groupid);
};
```

#### 11.FriendModel类

定义了好友相关业务的操作接口方法

```c++
#include "user.hpp"
class FriendModel
{
public:
    void insert(int userid, int friendid); // 添加好友关系

    vector<User> query(int userid); // 返回用户好友列表
};
```

#### 12.OfflineMsgModel类

定义了离线消息相关业务的操作接口方法

```c++
class OfflineMsgModel
{
public:
    void insert(int userid, string msg); // 存储用户的离线消息

    void remove(int userid); // 删除用户的离线消息

    vector<string> query(int userid); // 查询用户的离线消息
};
```



### ChatServer客户端

#### 线程相关

使用主线程发送请求，子线程接收响应

开辟一个子线程接收响应，并设置线程分离

```c++
 	// 连接服务器成功
	/*发送请求*/
	//启动接收子线程
    std::thread readTask(readTaskHandler, clientfd); // pthread_create
    readTask.detach(); // pthread_detach
```

readTaskHandler()函数负责监听clientfd上有没有响应数据，有就进行响应分发

```c++
// 子线程 - 接收线程
void readTaskHandler(int clientfd)
{
    for (;;)
    {
        char buffer[1024] = {0};
        int len = recv(clientfd, buffer, 1024, 0); // 没有就阻塞等待

        // 接收ChatServer转发的数据，反序列化生成json数据对象
        json js = json::parse(buffer);
        int msgtype = js["msgid"].get<int>();
        // 一对一消息显示处理
        if (ONE_CHAT_MSG == msgtype){/*调用相应函数处理*/}
        // 群消息显示处理
        if (GROUP_CHAT_MSG == msgtype){/*调用相应函数处理*/}
        // 处理登录
        if (LOGIN_MSG_ACK == msgtype){/*调用相应函数处理*/}
        // 处理注册
        if (REG_MSG_ACK == msgtype){/*调用相应函数处理*/}
        // 处理刷新
        if (REFRESH_LIST_MSG == msgtype){/*调用相应函数处理*/}
    }
}
```

#### 信号量相关

定义信号量对象

```c++
sem_t rwsem; // 用于读写线程之间的通信
```

初始化

```c++
sem_init(&rwsem, 0, 0);
```

主线程发送登录、注册请求后，调用sem_wait()函数等待子线程通知

```c++
//主线程
sem_wait(&rwsem);
```

子线程接收到响应并处理完之后，通知主线程

```c++
//子线程
sem_post(&rwsem); 
```

当需要退出时，销毁信号量

```c++
sem_destroy(&rwsem);
```

