#pragma once

#include <iostream>
#include <string>

// 1. 引入你之前写好的共享大抽屉
#include "SharedState.h" 

// 2. 引入 Livox-SDK2 的核心头文件 (路径根据你的实际 include 目录调整)
extern "C" {
#include "livox_lidar_def.h"
#include "livox_lidar_api.h"
}

class LivoxDriver {
public:
    // 构造函数：把咱们的大抽屉传进来，方便以后往里塞数据
    LivoxDriver(SharedState& shared_state);
    ~LivoxDriver();

    // 初始化雷达环境 (传入一个 config.json 配置文件的路径)
    bool Init(const std::string& config_path);

    // 静态(static)函数。
    static void PointCloudCallback(uint32_t handle, 
                                   const uint8_t dev_type, 
                                   LivoxLidarEthernetPacket* data, 
                                   void* client_data);


private:
    
    SharedState& shared_state_; 
};