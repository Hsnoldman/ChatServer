#include "chatservice.hpp"
#include "public.hpp"
#include <muduo/base/Logging.h>
#include <vector>
using namespace std;
using namespace muduo;

// 获取单例对象的接口函数
ChatService *ChatService::instance()
{
    static ChatService service;
    return &service;
}

// ChatService类的构造函数
//  注册消息以及对应的Handler回调操作
ChatService::ChatService()
{
    // 用户基本业务管理相关事件处理回调注册
    // 登陆
    _msgHandlerMap.insert({LOGIN_MSG, std::bind(&ChatService::login, this, _1, _2, _3)});
    // 登出
    _msgHandlerMap.insert({LOGINOUT_MSG, std::bind(&ChatService::loginout, this, _1, _2, _3)});
    // 注册
    _msgHandlerMap.insert({REG_MSG, std::bind(&ChatService::reg, this, _1, _2, _3)});
    // 一对一聊天
    _msgHandlerMap.insert({ONE_CHAT_MSG, std::bind(&ChatService::oneChat, this, _1, _2, _3)});
    // 添加好友
    _msgHandlerMap.insert({ADD_FRIEND_MSG, std::bind(&ChatService::addFriend, this, _1, _2, _3)});

    // 群组业务管理相关事件处理回调注册
    // 创建群组
    _msgHandlerMap.insert({CREATE_GROUP_MSG, std::bind(&ChatService::createGroup, this, _1, _2, _3)});
    // 加入群组
    _msgHandlerMap.insert({ADD_GROUP_MSG, std::bind(&ChatService::addGroup, this, _1, _2, _3)});
    // 群聊天
    _msgHandlerMap.insert({GROUP_CHAT_MSG, std::bind(&ChatService::groupChat, this, _1, _2, _3)});

    // 刷新列表
    _msgHandlerMap.insert({REFRESH_LIST_MSG, std::bind(&ChatService::refresh, this, _1, _2, _3)});

    // 连接redis服务器
    if (_redis.connect())
    {
        // 设置上报消息的回调
        _redis.init_notify_handler(std::bind(&ChatService::handleRedisSubscribeMessage, this, _1, _2));
    }
}

// 获取消息对应的处理器
MsgHandler ChatService::getHandler(int msgid)
{
    // 记录错误日志，msgid没有对应的事件处理回调
    auto it = _msgHandlerMap.find(msgid);
    if (it == _msgHandlerMap.end())
    {
        // 返回一个默认的处理器，空操作
        // lambda表达式 = 按值获取
        return [=](const TcpConnectionPtr &conn, json &js, Timestamp)
        {
            LOG_ERROR << "msgid:" << msgid << " can not find handler!";
        };
    }
    else // 返回对应的业务处理函数
    {
        return _msgHandlerMap[msgid];
    }
}

//通知好友上线
void ChatService::Notifyonlinefriends(vector<User> friendvec, User me)
{
    if (!friendvec.empty())
    {
        for (User &user : friendvec)
        {
            json js;
            js["msgid"] = ONE_CHAT_MSG;
            js["id"] = me.getId();
            js["name"] = me.getName();
            js["toid"] = user.getId();
            js["msg"] = "I'm online now, chat with me.";
            js["onlinemsg"] = 1;
            js["time"] = getCurrentTime();
            if (user.getState() == "online")
            {
                lock_guard<mutex> lock(_connMutex);
                auto it = _userConnMap.find(user.getId());
                if (it != _userConnMap.end())
                {

                    it->second->send(js.dump());
                    return;
                }
                else
                {
                    // 通过redis发布消息
                    _redis.publish(user.getId(), js.dump());
                }
            }
        }
    }
}

// 处理登录业务  id  pwd   pwd
void ChatService::login(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int id = js["id"].get<int>();
    string pwd = js["password"];

    User user = _userModel.query(id);
    if (user.getId() == id && user.getPwd() == pwd) // 进行用户名和密码判断
    {
        if (user.getState() == "online") // 该用户已经登录，不允许重复登录
        {
            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 2;
            response["errmsg"] = "this account is using, input another!";
            conn->send(response.dump()); // dump()将数据写成json形式
        }
        else
        {
            // 登录成功，记录用户连接信息
            {
                // 在服务器运行过程中进行处理，需要考虑线程安全问题，添加线程互斥操作
                lock_guard<mutex> lock(_connMutex);
                _userConnMap.insert({id, conn});
            }

            // id用户登录成功后，向redis订阅channel(id)
            _redis.subscribe(id);

            // 登录成功，更新用户状态信息 state offline -> online
            user.setState("online");
            _userModel.updateState(user);

            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 0;
            response["id"] = user.getId();
            response["name"] = user.getName();

            // 查询该用户是否有离线消息
            vector<string> vec = _offlineMsgModel.query(id);
            if (!vec.empty())
            {
                response["offlinemsg"] = vec;
                // 读取该用户的离线消息后，把该用户的所有离线消息删除掉
                _offlineMsgModel.remove(id);
            }

            // 查询该用户的好友信息并返回
            vector<User> userVec = _friendModel.query(id);
            if (!userVec.empty())
            {
                vector<string> vec2;
                for (User &user : userVec)
                {
                    json js;
                    js["id"] = user.getId();
                    js["name"] = user.getName();
                    js["state"] = user.getState();
                    vec2.push_back(js.dump());
                }
                response["friends"] = vec2;
            }

            // 通知所有在线好友
            Notifyonlinefriends(userVec, user);

            // 查询用户的群组信息
            vector<Group> groupuserVec = _groupModel.queryGroups(id);
            if (!groupuserVec.empty())
            {
                // group:[{groupid:[xxx, xxx, xxx, xxx]}]
                vector<string> groupV;
                for (Group &group : groupuserVec)
                {
                    json grpjson;
                    grpjson["id"] = group.getId();
                    grpjson["groupname"] = group.getName();
                    grpjson["groupdesc"] = group.getDesc();
                    vector<string> userV;
                    for (GroupUser &user : group.getUsers())
                    {
                        json js;
                        js["id"] = user.getId();
                        js["name"] = user.getName();
                        js["state"] = user.getState();
                        js["role"] = user.getRole();
                        userV.push_back(js.dump());
                    }
                    grpjson["users"] = userV;
                    groupV.push_back(grpjson.dump());
                }

                response["groups"] = groupV;
            }

            conn->send(response.dump());
        }
    }
    else
    {
        // 该用户不存在或用户存在但是密码错误，登录失败
        json response;
        response["msgid"] = LOGIN_MSG_ACK;
        response["errno"] = 1;
        response["errmsg"] = "id or password is invalid!";
        conn->send(response.dump());
    }
}

// 处理注销业务
void ChatService::loginout(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();

    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(userid);
        if (it != _userConnMap.end())
        {
            _userConnMap.erase(it);
        }
    }

    // 用户注销，相当于就是下线，在redis中取消订阅通道
    _redis.unsubscribe(userid);

    // 更新用户的状态信息
    User user(userid, "", "", "offline");
    _userModel.updateState(user);
}

// 处理注册业务  name  password
void ChatService::reg(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    string name = js["name"];
    string pwd = js["password"];

    User user;
    user.setName(name);
    user.setPwd(pwd);
    bool state = _userModel.insert(user);
    if (state)
    {
        // 注册成功
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 0; // 0表示响应成功
        response["id"] = user.getId();
        conn->send(response.dump()); // dump()将数据写成json文件
    }
    else
    {
        // 注册失败
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 1;
        conn->send(response.dump());
    }
}

// 处理客户端异常退出
void ChatService::clientCloseException(const TcpConnectionPtr &conn)
{
    User user;
    {
        lock_guard<mutex> lock(_connMutex); // 线程安全
        for (auto it = _userConnMap.begin(); it != _userConnMap.end(); ++it)
        {
            if (it->second == conn)
            {
                // 从map表删除用户的连接信息
                user.setId(it->first);
                _userConnMap.erase(it);
                break;
            }
        }
    }

    // 用户注销，相当于就是下线，在redis中取消订阅通道
    _redis.unsubscribe(user.getId());

    // 更新用户的状态信息
    if (user.getId() != -1)
    {
        user.setState("offline");
        _userModel.updateState(user); // 更新数据库表中的信息
    }
}

// 服务器异常，业务重置方法
void ChatService::reset()
{
    // 把所有online状态的用户，设置成offline
    _userModel.resetState();
}

// 一对一聊天业务
void ChatService::oneChat(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int toid = js["toid"].get<int>();

    {
        lock_guard<mutex> lock(_connMutex);
        // 查询当前用户是否在线
        // 先在本地服务器查询
        auto it = _userConnMap.find(toid);
        if (it != _userConnMap.end())
        {
            // toid在线，转发消息   主动推送服务器消息给toid用户
            it->second->send(js.dump());
            return;
        }
    }

    // 在数据库表中查询查询toid是否在线
    User user = _userModel.query(toid);
    // 如果数据库表中toid状态为在线，则说明toid用户在其他服务器
    if (user.getState() == "online")
    {
        // 通过redis发布消息
        _redis.publish(toid, js.dump());
        return;
    }

    // toid不在线，存储离线消息
    _offlineMsgModel.insert(toid, js.dump());
}

// 添加好友业务 msgid id friendid
void ChatService::addFriend(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    int friendid = js["friendid"].get<int>();

    // 存储好友信息
    _friendModel.insert(userid, friendid);
}

// 创建群组业务
void ChatService::createGroup(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    string name = js["groupname"];
    string desc = js["groupdesc"];

    // 存储新创建的群组信息
    Group group(-1, name, desc); // 此时群组数据还未传入数据库，groupID未知，传入-1
    if (_groupModel.createGroup(group))
    {
        // 存储群组创建人信息，群主
        _groupModel.addGroup(userid, group.getId(), "creator");
    }
}

// 加入群组业务
void ChatService::addGroup(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();
    _groupModel.addGroup(userid, groupid, "normal");
}

// 群组聊天业务
void ChatService::groupChat(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();
    vector<int> useridVec = _groupModel.queryGroupUsers(userid, groupid);

    lock_guard<mutex> lock(_connMutex);
    for (int id : useridVec)
    {
        auto it = _userConnMap.find(id);
        if (it != _userConnMap.end())
        {
            // 转发群消息
            it->second->send(js.dump());
        }
        else
        {
            // 查询toid是否在线
            User user = _userModel.query(id);
            // 说明用户在其他服务器，通过redis发布消息
            if (user.getState() == "online")
            {
                _redis.publish(id, js.dump());
            }
            else
            {
                // 存储离线群消息
                _offlineMsgModel.insert(id, js.dump());
            }
        }
    }
}

// 刷新列表业务
void ChatService::refresh(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int id = js["id"].get<int>();
    User user = _userModel.query(id);

    json response;
    response["msgid"] = REFRESH_LIST_MSG;
    response["errno"] = 0;
    response["id"] = user.getId();
    response["name"] = user.getName();

    // 查询该用户的好友信息并返回
    vector<User> userVec = _friendModel.query(id);
    if (!userVec.empty())
    {
        vector<string> vec2;
        for (User &user : userVec)
        {
            json js;
            js["id"] = user.getId();
            js["name"] = user.getName();
            js["state"] = user.getState();
            vec2.push_back(js.dump());
        }
        response["friends"] = vec2;
    }

    // 查询用户的群组信息
    vector<Group> groupuserVec = _groupModel.queryGroups(id);
    if (!groupuserVec.empty())
    {
        // group:[{groupid:[xxx, xxx, xxx, xxx]}]
        vector<string> groupV;
        for (Group &group : groupuserVec)
        {
            json grpjson;
            grpjson["id"] = group.getId();
            grpjson["groupname"] = group.getName();
            grpjson["groupdesc"] = group.getDesc();
            vector<string> userV;
            for (GroupUser &user : group.getUsers())
            {
                json js;
                js["id"] = user.getId();
                js["name"] = user.getName();
                js["state"] = user.getState();
                js["role"] = user.getRole();
                userV.push_back(js.dump());
            }
            grpjson["users"] = userV;
            groupV.push_back(grpjson.dump());
        }

        response["groups"] = groupV;
    }

    conn->send(response.dump());
}

// 从redis消息队列中获取订阅的消息
void ChatService::handleRedisSubscribeMessage(int userid, string msg) // userid作为通道号
{
    lock_guard<mutex> lock(_connMutex);
    auto it = _userConnMap.find(userid);
    if (it != _userConnMap.end())
    {
        it->second->send(msg);
        return;
    }

    // 存储该用户的离线消息
    _offlineMsgModel.insert(userid, msg);
}

// 获取系统时间（聊天信息需要添加时间信息）
string ChatService::getCurrentTime()
{
    auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    struct tm *ptm = localtime(&tt);
    char date[60] = {0};
    sprintf(date, "%d-%02d-%02d %02d:%02d:%02d",
            (int)ptm->tm_year + 1900, (int)ptm->tm_mon + 1, (int)ptm->tm_mday,
            (int)ptm->tm_hour, (int)ptm->tm_min, (int)ptm->tm_sec);
    return std::string(date);
}

// json格式
//  注册
//{"msgid":4,"name":"bob","password":"123456"}

// 登录
//  {"msgid":1,"id":1,"password":"123456"}
//  {"msgid":1,"id":2,"password":"123456"}

// 聊天
//{"msgid":6,"id":1,"from":"Jack","toid":2,"msg":"hello1."}
//{"msgid":6,"id":2,"from":"Bob","toid":1,"msg":"Hi."}

// 添加好友
//{"msgid":7,"id":1,"friendid":2}
//{"msgid":7,"id":2,"friendid":1}
