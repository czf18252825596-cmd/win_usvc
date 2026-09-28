// 1. 引入 Windows 网络通信核心库
#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstring>
#include <string>
#include <cstdint>
#include <iostream>

#pragma comment(lib, "ws2_32.lib") //编译链接库， Winsock API 必须链接这个库。

class Udp 
{
public:
    Udp(int sport);
    ~Udp();

private:
    int server_port_;

    SOCKET udp_socket_;//int 类型修改为 Windows 下的 SOCKET 类型

    std::string target_ip_;
    int target_port_;

    

public:
    //已连接的 UDP 套接字，只接收固定对端的数据
    bool udp_send(uint8_t *pBuf, uint16_t len); //pBuf 是数据缓冲区，len 是数据长度
    bool udp_receive(uint8_t *pBuf, uint16_t *pLen);
    //服务器接收任意客户端的数据,通过IP跟端口号Port
    bool udp_sendto(uint8_t *pBuf, uint16_t len, std::string ip, int port);
    bool udp_receivefrom(uint8_t *pBuf, uint16_t *pLen, std::string *pIp, int *pPort);
};

