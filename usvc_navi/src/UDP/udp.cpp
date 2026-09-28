#define _WINSOCK_DEPRECATED_NO_WARNINGS
// 消除 Windows 下 inet_addr 和 inet_ntoa 的安全警告
#include "udp.h"

Udp::Udp(int sport)
{
    server_port_    = sport;

    // 1. Windows 初始化 Winsock 环境
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {//使用 Winsock 2.2 版本
        std::cerr << "WSAStartup failed with error: " << WSAGetLastError() << std::endl;
        //std::cerr 配合 WSAGetLastError() 获取具体的 Windows 网络错误码
        udp_socket_ = INVALID_SOCKET;
        return;
    }
    //socket
    udp_socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_socket_ == INVALID_SOCKET) 
    {
        std::cerr << "socket creation failed with error: " << WSAGetLastError() << std::endl;
        return;
    }
    // 设置为非阻塞模式
    u_long mode = 1;
    if (ioctlsocket(udp_socket_, FIONBIO, &mode) != NO_ERROR) { //Windows 需要替换成专用的 ioctlsocket() 函数
        std::cerr << "ioctlsocket failed with error: " << WSAGetLastError() << std::endl;
    }

    // 设置服务器地址
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port_);
    server_addr.sin_addr.s_addr = INADDR_ANY;  // 修改为INADDR_ANY接收广播

    // 绑定服务器地址
    if (bind(udp_socket_, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) 
    {   std::cerr << "bind failed with error: " << WSAGetLastError() << std::endl;
        closesocket(udp_socket_);//win用closesocket() 关闭
        udp_socket_ = INVALID_SOCKET;
    }
}

Udp::~Udp()
{
    //  判定条件与关闭函数
    if (udp_socket_ != INVALID_SOCKET) 
    {
        closesocket(udp_socket_); //win用closesocket() 关闭
    }
    // 释放 Winsock 资源
    WSACleanup();
}

bool Udp::udp_send(uint8_t *pBuf, uint16_t len)
{
    if (udp_socket_ == INVALID_SOCKET) // 替换判定条件
        return false;

    struct sockaddr_in target_addr;
    memset(&target_addr, 0, sizeof(target_addr));
    target_addr.sin_family = AF_INET;   // 地址族，AF_INET = IPv4。如果是 IPv6 就是 AF_INET6
    target_addr.sin_port = htons(target_port_);  //把主机字节序的端口号转成网络字节序,PC 是小端（低字节在前），网络是大端（高字节在前)
    target_addr.sin_addr.s_addr = inet_addr(target_ip_.c_str());
    
    int sent = sendto(udp_socket_, (const char*)pBuf, len, 0, 
                         (struct sockaddr*)&target_addr, sizeof(target_addr));

    return sent == len;
}

bool Udp::udp_receive(uint8_t *pBuf, uint16_t *pLen)
{
    if (udp_socket_ == INVALID_SOCKET) 
        return false;

    struct sockaddr_in client_addr;
    int addr_len = sizeof(client_addr);
    
    int received = recvfrom(udp_socket_, (char*)pBuf, 1024, 0, 
                               (struct sockaddr*)&client_addr, &addr_len);
    if (received > 0) 
    {
        *pLen = received;
        target_ip_ = inet_ntoa(client_addr.sin_addr);
        target_port_ = ntohs(client_addr.sin_port);

        return true;
    }

    return false;
}

bool Udp::udp_sendto(uint8_t *pBuf, uint16_t len, std::string ip, int port)
{
    if (udp_socket_ == INVALID_SOCKET) 
        return false;
    
    struct sockaddr_in target_addr;
    memset(&target_addr, 0, sizeof(target_addr));
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(port);
    target_addr.sin_addr.s_addr = inet_addr(ip.c_str());
    
    int sent = sendto(udp_socket_, (const char*)pBuf, len, 0, 
                         (struct sockaddr*)&target_addr, sizeof(target_addr));

    return sent == len;
}

bool Udp::udp_receivefrom(uint8_t *pBuf, uint16_t *pLen, std::string *pIp, int *pPort)
{
    if (udp_socket_ == INVALID_SOCKET) 
        return false;
    
    struct sockaddr_in client_addr;
    int addr_len = sizeof(client_addr);
    
    int received = recvfrom(udp_socket_, (char*)pBuf, 1024, 0, 
                               (struct sockaddr*)&client_addr, &addr_len);
    if (received > 0) 
    {
        *pLen = received;
        *pIp = inet_ntoa(client_addr.sin_addr);
        *pPort = ntohs(client_addr.sin_port);
        return true;
    }

    return false;
}
