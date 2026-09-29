#include <iostream>
#include <windows.h>
#include "SharedState.h"
#include "lidar_driver.h"

// 引入 Livox SDK 的数据结构以伪造数据
extern "C" {
#include "livox_lidar_def.h"
}

int main() 
{   
    SetConsoleOutputCP(CP_UTF8);
    std::cout << "--- 开始雷达脱机模拟测试 ---" << std::endl;

    // 1. 初始化依赖对象
    SharedState shared_state;
    LivoxDriver driver(shared_state);

    // 2. 伪造一个 LivoxLidarEthernetPacket 数据包内存块
    // 假设一个包里有 96 个点
    const int POINT_NUM = 96;
    int packet_size = sizeof(LivoxLidarEthernetPacket) + sizeof(LivoxLidarCartesianHighRawPoint) * POINT_NUM;
    
    // 申请一块连续内存来装这个包
    uint8_t* fake_buffer = new uint8_t[packet_size];
    
    // 3. 开始“造假”
    LivoxLidarEthernetPacket* fake_packet = reinterpret_cast<LivoxLidarEthernetPacket*>(fake_buffer);
    fake_packet->data_type = kLivoxLidarCartesianCoordinateHighData; // 设置为高精度笛卡尔坐标系
    fake_packet->length = packet_size; // 设置总包长

    // 拿到装点云的数组指针，开始往里塞假坐标
    LivoxLidarCartesianHighRawPoint* fake_points = reinterpret_cast<LivoxLidarCartesianHighRawPoint*>(fake_packet->data);
    for(int i = 0; i < POINT_NUM; i++) 
    {
        // 注意：Livox 底层单位是毫米 (mm)
        fake_points[i].x = 1000; // 伪造 x = 1米
        fake_points[i].y = 2000; // 伪造 y = 2米
        fake_points[i].z = 10;   // 伪造 z = 0.01米
    }

    // 4. 模拟雷达高频发包 (循环 20 次，每次间隔 10ms，总计约 200ms)
    std::cout << "正在向回调函数轰炸假数据..." << std::endl;
    for(int i = 0; i < 20; i++) 
    {
        // 强行调用静态回调函数，假装 SDK 触发了它！
        // 关键：最后一个参数必须传 &driver，把对象钥匙传进去
        LivoxDriver::PointCloudCallback(0, 0, fake_packet, &driver);
        
        Sleep(10); // 停顿 10 毫秒
    }

    // 清理内存
    delete[] fake_buffer;

    std::cout << "--- 测试结束 ---" << std::endl;
    return 0;
}