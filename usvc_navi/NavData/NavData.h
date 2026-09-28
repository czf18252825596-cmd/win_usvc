#pragma once
#include <cstdint>

// 导航数据：由 usvc_navi 解析 ULink 帧后填充，供其他模块读取
struct NavData {
    uint32_t time_tick = 0;
    uint8_t  ins_sta   = 0;        // 组合导航状态 0初始 1正常 2异常
    uint8_t  pos_sta   = 0;        // 定位状态 0初始 1有定位 2无定位
    uint8_t  heading_sta = 0;      // 定向状态 0初始 1有定向 2无定向
    uint8_t  satellite_count = 0;  // 收星数
    double position_std_lat = 0;   // 纬度精度(m)
    double position_std_lon = 0;   // 经度精度(m)

    double gyro_x = 0, gyro_y = 0, gyro_z = 0;   // 陀螺(deg/s)
    double roll = 0, pitch = 0, heading = 0;     // 姿态(度)
    double acc_x = 0, acc_y = 0, acc_z = 0;      // 加速度(g)
    double vel_x = 0, vel_y = 0, vel_z = 0;      // 速度xyz(m/s)
    double latitude = 0, longitude = 0, altitude = 0;
    double vel_n = 0, vel_e = 0, vel_d = 0;      // 速度ned(m/s)
};