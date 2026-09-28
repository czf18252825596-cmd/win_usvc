#include <iostream>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN   // 禁止 windows.h 引入 winsock.h，避免与 winsock2.h 冲突
#endif
#include <windows.h> // 提供 Sleep() 函数

#include "udp.h"
extern "C" {
#include "ULink.h"
}

// 模拟器只发送、不接收：把 ULink 组好的帧固定发给本机 6001 端口
Udp* g_udp = nullptr;

bool sim_send(void*, uint8_t* buf, uint16_t len) {
    return g_udp->udp_sendto(buf, len, "127.0.0.1", 6001);
}

int main() {
    Udp udp(5900);          // 绑一个空端口，仅用于发
    g_udp = &udp;

    ULink_t mULink = {};
    ULink_RegSendFunc(&mULink, sim_send, g_udp);

    uint32_t tick = 0;
    double heading = 0.0;

    while (true) {
        DATA_NAVI_t nav{};
        nav.ins_sta = 1;
        nav.timeTick = tick++;
        nav.gyro_heading = 0.05;                     // 缓慢转向
        nav.heading = heading;
        nav.roll = 0.0;
        nav.pitch = 0.0;
        nav.acc_z = 9.8;                             // 平稳水面近似
        nav.vel_n = 1.5;                             // 北向 1.5 m/s
        nav.vel_e = 0.3;
        nav.vel_d = 0.0;
        nav.latitude  = 34.5858727376671 + 0.000001 * (tick % 100);
        nav.longitude = 113.681501471667  + 0.000001 * (tick % 100);
        nav.altitude = 2.0;
        nav.satellite_count = 12;
        nav.pos_sta = 1;
        nav.heading_sta = 1;
        nav.position_std_lat = 0.02;
        nav.position_std_lon = 0.02;

        heading += 0.5;
        if (heading >= 360.0) heading -= 360.0;

        if (!ULink_Send(&mULink, 0x00, 0x00, PACKET_ID_NAVI,
                        (uint8_t*)&nav, sizeof(nav))) {
            std::cerr << "send failed" << std::endl;
        }
        Sleep(50);   // 20Hz
    }
    return 0;
}