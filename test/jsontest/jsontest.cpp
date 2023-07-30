#include "../../thirdparty/json.hpp"
using json = nlohmann::json;

#include <iostream>
#include <vector>
#include <map>
#include <string>
using namespace std;

// json序列化实例1
string func1()
{
    json js;
    // 添加数组
    js["id"] = {1, 2, 3, 4, 5};
    // 添加key-value
    js["name"] = "zhang san";

    // 添加对象
    js["msg"]["zhang san"] = "hello world";
    js["msg"]["liu shuo"] = "hello china";
    // 上面等同于下面这句一次性添加数组对象
    // js["msg"] = {{"zhang san", "hello world"}, {"liu shuo", "hello china"}};

    // cout << js << endl;

    string sendBuf = js.dump(); // json数据对象->序列化 json字符串
    // c_str() 函数可以将 const string* 类型 转化为 const char* 类型
    // cout << sendBuf.c_str() << endl;
    return sendBuf;
}

// json序列化实例2
string func2()
{
    json js;

    // 直接序列化一个vector容器
    vector<int> vec;
    vec.push_back(1);
    vec.push_back(2);
    vec.push_back(5);
    js["list"] = vec;

    // 直接序列化一个map容器
    map<int, string> m;
    m.insert({1, "a1"});
    m.insert({2, "a2"});
    m.insert({3, "a3"});
    js["path"] = m;
    // cout << js << endl;
    // cout << js["path"] << endl;

    string sendBuf = js.dump(); // json数据对象->序列化 json字符串
    // cout << sendBuf.c_str() << endl;

    return sendBuf;
}

int main()
{
    string recvBuf1 = func1();
    // 数据反序列化 json字符串 -> 数据对象
    json jsbuf = json::parse(recvBuf1);
    cout << jsbuf["id"] << endl;
    cout << jsbuf["name"] << endl;

    string recvBuf2 = func2();
    json jsbuf2 = json::parse(recvBuf2);

    vector<int> arr = jsbuf2["list"];
    arr.push_back(6);
    for (auto &s : arr)
    {
        cout << s << " ";
    }
    cout << endl;

    return 0;
}