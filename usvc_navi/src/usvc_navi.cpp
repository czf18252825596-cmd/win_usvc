#ifndef WIN32_LEAN_AND_MEAN//避免 windows.h 引入 winsock.h，避免与 winsock2.h 冲突
#define WIN32_LEAN_AND_MEAN
#endif
#include <iostream>
#include <string>


#include <windows.h> // 提供 Sleep() 函数
#include "config/Config.h"
#include "NavData/NavData.h"
#include "Common/SharedState.h"
#include "UDP/udp.h"
extern "C" {
#include "ULink/ULink.h"
}

// 包装函数，用于将成员函数转为带上下文的 C 函数
bool UDP_SendBuffer(void* context, uint8_t* buf, uint16_t len) {
    Udp* mUdp = static_cast<Udp*>(context);
    return mUdp->udp_send(buf, len);
}

bool UDP_RecvBuffer(void* context, uint8_t* buf, uint16_t* len) {
    Udp* mUdp = static_cast<Udp*>(context);
    return mUdp->udp_receive(buf, len);
}

int main(int argc, char** argv) 
{
 
    Config config; 
    if (!config.Load ( "Config/setting.ini" )) {
        std::cerr << "读取配置失败: Config/setting.ini\n" ;
        return 1;
    } 
    const int port = config. Get <int>("navigation", "local_port", 6001 );

    SharedState g_shared_state;
    //定义ULink
    ULink_t mULink = {};
    //实例UDP
    Udp mUdp(port);

    //注册ULink收发函数
    ULink_RegSendFunc(&mULink, UDP_SendBuffer, &mUdp);
    ULink_RegReciveFunc(&mULink, UDP_RecvBuffer, &mUdp);

    while (true) 
    {
        uint8_t DEVICE_ID;
        uint8_t PACKET_ID; 
        uint8_t MS;
        uint8_t *pDATA; 
        uint16_t LENGTH;

        //接收并解析导航数据
        if(ULink_Receive(&mULink, &DEVICE_ID, &MS, &PACKET_ID,  &pDATA, &LENGTH))
		{
           if (PACKET_ID == PACKET_ID_NAVI) 
                {
                    NavData nav;
                    nav.ins_sta          = ((DATA_NAVI_t*)pDATA)->ins_sta;
                    nav.time_tick        = ((DATA_NAVI_t*)pDATA)->timeTick;
                    nav.gyro_x           = ((DATA_NAVI_t*)pDATA)->gyro_roll;
                    nav.gyro_y           = ((DATA_NAVI_t*)pDATA)->gyro_pitch;
                    nav.gyro_z           = ((DATA_NAVI_t*)pDATA)->gyro_heading;
                    nav.roll             = ((DATA_NAVI_t*)pDATA)->roll;
                    nav.pitch            = ((DATA_NAVI_t*)pDATA)->pitch;
                    nav.heading          = ((DATA_NAVI_t*)pDATA)->heading;
                    nav.acc_x            = ((DATA_NAVI_t*)pDATA)->acc_x;

                    nav.acc_y            = ((DATA_NAVI_t*)pDATA)->acc_y;
                    nav.acc_z            = ((DATA_NAVI_t*)pDATA)->acc_z;
                    nav.vel_x            = ((DATA_NAVI_t*)pDATA)->vel_x;
                    nav.vel_y            = ((DATA_NAVI_t*)pDATA)->vel_y;
                    nav.vel_z            = ((DATA_NAVI_t*)pDATA)->vel_z;
                    nav.latitude         = ((DATA_NAVI_t*)pDATA)->latitude;
                    nav.longitude        = ((DATA_NAVI_t*)pDATA)->longitude;
                    nav.altitude         = ((DATA_NAVI_t*)pDATA)->altitude;
                    nav.vel_n            = ((DATA_NAVI_t*)pDATA)->vel_n;
                    nav.vel_e            = ((DATA_NAVI_t*)pDATA)->vel_e;
                    nav.vel_d            = ((DATA_NAVI_t*)pDATA)->vel_d;
                    nav.satellite_count  = ((DATA_NAVI_t*)pDATA)->satellite_count;
                    nav.pos_sta          = ((DATA_NAVI_t*)pDATA)->pos_sta;
                    nav.heading_sta      = ((DATA_NAVI_t*)pDATA)->heading_sta;
                    nav.position_std_lat = ((DATA_NAVI_t*)pDATA)->position_std_lat;
                    nav.position_std_lon = ((DATA_NAVI_t*)pDATA)->position_std_lon;

                    // 加独占锁，将最新导航帧存入共享抽屉
                    g_shared_state.WriteNav(nav);

                    std::cout << "nav: lon=" << nav.longitude
                              << " lat=" << nav.latitude
                              << " heading=" << nav.heading << std::endl;

                }
            
        }
        Sleep(10);   // 对齐原来的 100Hz 轮询
    }

    return 0;
}
