// main.cpp
#include <iostream>
#include <thread>
#include <atomic>
#include <csignal>
#include "SharedState.h"

// 唯一的全局状态，所有线程共用！
SharedState g_shared_state;             
std::atomic<bool> g_running{true};

// 统一的退出信号处理
void signalHandler(int) {
    std::cout << "\n[MAIN] 收到退出信号，正在安全关闭所有线程..." << std::endl;
    g_running = false;
}

int main() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::cout << "[MAIN] 系统启动..." << std::endl;

    // TODO: 1. 实例化类 (传引用)
    // Upper upper(g_shared_state);
    // Ship  ship(g_shared_state);

    // TODO: 2. 初始化 (读配置文件等)
    // upper.Init();
    // ship.Init();

    // TODO: 3. 起线程，各跑各的 Run()
    // std::thread t_upper(&Upper::Run, &upper, &g_running);
    // std::thread t_ship (&Ship::Run,  &ship,  &g_running);

    // TODO: 4. 阻塞等待结束
    // t_upper.join();
    // t_ship.join();

    std::cout << "[MAIN] 系统安全退出。" << std::endl;
    return 0;
}