// upper_node.h
#pragma once

#include <atomic>
#include <vector>
#include <string>
#include <mutex>
#include <memory>
#include <boost/asio.hpp>
#include "SharedState.h"

extern "C" {
#include "MTLink/MTLink.h"
}

class Upper {
public:
    // 构造函数：强绑定全局抽屉
    Upper(SharedState& state);
    ~Upper();

    // 初始化（原 main 函数里读取 config.yaml 的部分）
    bool Init();

    // 主循环执行入口（原 main 函数里的 while(g_running) 部分）
    void Run(std::atomic<bool>* running);

private:
    SharedState& state_; // 内部保存全局抽屉的引用

    // ==========================================
    // 1. 原来的全局变量，现在统统变成私有成员！
    // ==========================================
    int BROAD_PORT = 5000;
    int DEVICE_ID = 0;
    int DEVICE_PORT = 5001;
    std::string DEVICE_TYPE = "ship_calc";
    std::string DEVICE_SN = "NULL";

    MTLink_t m_MTLink_upper = {0}; // 注意：去掉了 g_ 前缀，改为 m_ 代表 member
    std::vector<uint8_t> m_json_buffer;

    std::mutex m_clients_mutex; 
    std::vector<std::shared_ptr<boost::asio::ip::tcp::socket>> m_clients; 
    std::mutex m_clients_to_remove_mutex;
    std::vector<size_t> m_clients_to_remove; 

    std::atomic<bool>* m_running; // 保存外面传进来的 running 指针，给子线程用

    // ==========================================
    // 2. 原来的三个子线程函数，变成私有成员函数
    // ==========================================
    void udpBroadcastThread();
    void tcpsUpperMTLinkAcceptThread();
    void tcpsUpperJsonThread();

    // ==========================================
    // 3. 【关键排雷】：给 C 语言用的回调函数必须是 static！
    // ==========================================
    static bool upperTcpsSendBuffer(void* context, uint8_t* buf, uint16_t len);
    static bool upperTcpsRecvBuffer(void* context, uint8_t* buf, uint16_t* len);
};