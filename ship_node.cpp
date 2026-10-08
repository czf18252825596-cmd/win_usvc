#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN // 防止宏冲突
#endif
#include <windows.h>        // 提供 Sleep()

#include "config/Config.h"  // INI 解析器
#include "SharedState.h"    // 线程安全数据抽屉

#include <boost/asio.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <thread>
#include <functional>

#include "config_manager_tool/config_manager_tool.h"
#include <csignal>
#include <atomic>

extern "C" {
#include "MTLink/MTLink.h"
}


using namespace boost::asio;
using namespace boost::asio::ip;
using boost::asio::buffer;

static int LISTEN_PORT;
static std::string DEVICE_SN;
static std::string SHIP_TYPE;

static MTLink_t g_MTLink_ship = {0};

static std::atomic<uint8_t> g_ship_connect_flag(0); //连接标志 0_无连接 1_连接 
// 全局socket
static std::mutex g_ship_socket_mutex;
static std::shared_ptr<tcp::socket> g_ship_socket;

static std::atomic<uint8_t> g_ship_server_ready(false); 
static std::string g_ship_server_ip;
static int g_ship_server_port;

//程序循环条件
static std::atomic<bool> g_running{true};
static void signalHandler(int) {
    g_running = false;
}

static SharedState shared_state;

// TCPC发送，用于注册MTLink
static bool shipTcpcSendBuffer(void* context, uint8_t* buf, uint16_t len) 
{
    bool state = false;

    std::lock_guard<std::mutex> lock(g_ship_socket_mutex);
    
    if (g_ship_socket && g_ship_socket->is_open()) 
    {
        boost::system::error_code ec;
        size_t bytes_sent = g_ship_socket->write_some(boost::asio::buffer(buf, len), ec);
        state |= (ec?false:true); 
    }

    return state;
}

// TCPC接收，用于注册MTLink
static bool shipTcpcRecvBuffer(void* context, uint8_t* buf, uint16_t* len) 
{
    bool state = false;

    std::shared_ptr<tcp::socket> ship_socket;
    {
        std::lock_guard<std::mutex> lock(g_ship_socket_mutex);
        ship_socket = g_ship_socket;  // 复制shared_ptr
    }


    if (!ship_socket || !ship_socket->is_open()) 
    {
        if(g_ship_connect_flag == 1)
        {
             g_ship_connect_flag = 0;  // 触发重连
        }
           

        return false;
    }
    
    boost::system::error_code ec;
    size_t bytes_read = ship_socket->read_some(boost::asio::buffer(buf, 2048), ec);
    *len = static_cast<uint16_t>(bytes_read);
    state |= (ec?false:true); 

    if(ec && ec != boost::asio::error::would_block)
    {
        if(g_ship_connect_flag == 1)
        {
            g_ship_connect_flag = 0;  // 触发重连
        }     
    }

    return state;
}

//UDP广播监听
static void udpListenThread(void)
{
    try {
        // 创建IO上下文和套接字
        io_context io_context_;
        udp::socket socket_(io_context_);
        
        // 创建监听端点
        udp::endpoint endpoint_(udp::v4(), LISTEN_PORT);
        
        // 配置套接字
        socket_.open(endpoint_.protocol());                     // 打开 socket
        socket_.set_option(socket_base::reuse_address(true));   // 允许地址重用
        socket_.set_option(socket_base::broadcast(true));       // 允许接收/发送广播
        socket_.bind(endpoint_);                                // 绑定到本地端口
        socket_.non_blocking(true);                             // 设为非阻塞

        printf("---[ship_node.cpp] udp listener started on port %d", LISTEN_PORT);
        
        // 接收缓冲区
        const size_t buf_size = 1024;
        char buf[buf_size];
        udp::endpoint sender_endpoint;
        
        while (g_running)
        {
            try {
                boost::system::error_code ec;
                size_t bytes_received = 0;
                
                // 尝试接收数据（非阻塞）
                bytes_received = socket_.receive_from(buffer(buf, buf_size), sender_endpoint, 0, ec); // sender_endpoint,   // 输出参数：发送方的 IP 和端口

                if (!ec && bytes_received > 0) 
                {
                    //解析JSON并获取船服务的IP和Port
                    try 
                    {
                        std::string json_str(buf, bytes_received);
                        std::istringstream iss(json_str);

                        boost::property_tree::ptree pt;
                        boost::property_tree::read_json(iss, pt);

                        int id = pt.get<int>("id");
                        std::string ip = pt.get<std::string>("ip");
                        int port = pt.get<int>("port");
                        std::string type = pt.get<std::string>("type");
                        std::string sn = pt.get<std::string>("sn");

                        if(type == SHIP_TYPE)
                        {
                            if(DEVICE_SN == "NULL")
                            {
                                printf("---[ship_node.cpp] first get ship sn");
                                g_ship_server_ip = ip;
                                g_ship_server_port = port;

                                g_ship_server_ready = true;

                                //将yaml文件中的DEVICE_SN修改为本次接收到的sn并保存
                                std::string config_file = "./config/config.yaml";

                                if (ConfigManagerTool::updateParamInFile(config_file, "DEVICE_SN", sn))
                                {
                                    // 更新内存中的DEVICE_SN值
                                    DEVICE_SN = sn;
                                    printf("---[ship_node.cpp] saved ship sn is %s", DEVICE_SN.c_str());
                                }
                            }
                            else
                            {
                                if(sn == DEVICE_SN)
                                {
                                    g_ship_server_ip = ip;
                                    g_ship_server_port = port;

                                    // g_ship_server_ip = "192.168.1.9";
                                    // g_ship_server_port = 3000;

                                    g_ship_server_ready = true;
                                }
                            }
                        }
                    }catch (const std::exception& e) {
                        std::cerr<< "ship_node.cpp ==== failed to parse broadcast message: " << e.what() << std::endl;
                    }
                }

                // 如果没有休眠，给其他线程执行机会
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

            } catch (const std::exception& e) {
                std::cerr<< "ship_node.cpp ==== Exception in UDP listener: " << e.what() << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }catch (const std::exception& e) {
        std::cerr<< "ship_node.cpp ==== Failed to initialize UDP listener on port " << LISTEN_PORT << ": " << e.what() << std::endl;
        return; // 初始化失败直接返回
    }
}


//TCP连接线程
static void tcpcShipMTLinkConnectThread(void)
{
    auto io_context = std::make_shared<boost::asio::io_context>();
 
   while (g_running)
    {  
        if(g_ship_connect_flag != 1)
        {
            if(g_ship_server_ready)
            {
                try {
                     // 创建新socket
                    auto new_socket = std::make_shared<tcp::socket>(*io_context);
                    new_socket->open(tcp::v4());
                    new_socket->non_blocking(true);  // 设置非阻塞

                    tcp::endpoint endpoint(address::from_string(g_ship_server_ip), g_ship_server_port);

                    boost::system::error_code ec;
                    new_socket->connect(endpoint, ec);

                    if (!ec) 
                    {
                        {
                            std::lock_guard<std::mutex> lock(g_ship_socket_mutex);
                            
                            // 关闭旧的socket
                            if(g_ship_socket && g_ship_socket->is_open())
                            {
                                g_ship_socket->close();
                            }
                            
                            // 设置新的socket
                            g_ship_socket = new_socket;
                        } 
                        // 立即连接成功
                        g_ship_connect_flag = 1;
                        printf("---[ship_node.cpp] ship connected to server %s:%d", g_ship_server_ip.c_str(), g_ship_server_port);
                    } 
                    else if (ec == boost::asio::error::in_progress) 
                    {
                        // 连接进行中，等待一下再检查
                        printf("---[ship_node.cpp] ship connection in progress...");
                        g_ship_connect_flag = 0;  // 保持连接标志，下次继续尝试
                    }
                    else
                    {
                        // 连接失败
                        printf("Warning---[ship_node.cpp] ship failed to connect: %s", ec.message().c_str());
                        std::this_thread::sleep_for(std::chrono::seconds(5));
                        g_ship_connect_flag = 0;  // 保持连接标志，下次继续尝试
                    }
                }catch (const boost::system::system_error& e) {
                    // 连接失败（服务没开、网络不通等）
                    std::cerr<< "ship_node.cpp ==== ship connect fail: " << e.what() << std::endl;
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                    g_ship_connect_flag = 0;  // 保持连接标志，下次继续尝试
                }
            }
        }

        // 处理异步IO事件
        io_context->restart();
        io_context->poll();
        
        // 短的睡眠时间，提高响应速度
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

int main(int argc, char** argv) 
{

    Config config; 
    if (!config.Load ( "common/Config/setting.ini" )) {
        std::cerr << "读取配置失败: Config/setting.ini\n" ;
        return 1;
    } 
    LISTEN_PORT = config.Get<int>("ship", LISTEN_PORT, 5000);
    DEVICE_SN   = config.Get<std::string>("ship", DEVICE_SN, "NULL");
    SHIP_TYPE   = config.Get<std::string>("ship", SHIP_TYPE, "ship_main");

    memset(&g_MTLink_ship, 0, sizeof(MTLink_t));
    MTLink_RegSendFunc(&g_MTLink_ship, shipTcpcSendBuffer, NULL);
    MTLink_RegReciveFunc(&g_MTLink_ship, shipTcpcRecvBuffer, NULL);

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::thread thread_udpListen(udpListenThread); //启动线程
    std::thread thread_tcpcShipMTLinkConnect(tcpcShipMTLinkConnectThread);

    while (g_running)
    {
        uint8_t SID;
		uint8_t DID;
		uint16_t Obj;
		uint8_t *pBuf;
		uint16_t Len;

        if(MTLink_Receive(&g_MTLink_ship, &SID, &DID, &Obj, &pBuf, &Len))
        {
            switch(Obj)
			{
                case OBJ_SHIP_BoardReset:
                case OBJ_SHIP_BoardInfo:
                case OBJ_SHIP_BoardVersion:
                case OBJ_SHIP_BoardNetwork:
                {
                    printf("---[ship_node.cpp]  publish raw obj is 0x%04x:",Obj);
                    // 工控机不处理以上指令，直接塞入“上传透传队列”，交给 upper_node 发给上位机
                    shared_state.PushRawDataUp(Obj, pBuf, Len);
                    break;
                }

                case OBJ_SHIP_SetID:
                {
                    //ID参数的处理，支持工控机端保存，并将指令同步转发给主板
                    MDATA_SHIP_ID_t SHIP_ID;
                    memcpy(&SHIP_ID, pBuf, Len);
                    // 存入共享sharestate
                    shared_state.WriteShipID(SHIP_ID);

                    break;
                }

                case OBJ_SHIP_BoardPort:
                case OBJ_SHIP_YawPIDParam:
                case OBJ_SHIP_SpeedPIDParam:
                case OBJ_SHIP_DisPIDParam:
                {   
                    // 工控机同样不处理以上PID和端口指令，直接塞入“上传透传队列”
                    shared_state.PushRawDataUp(Obj, pBuf, Len);
                    break;
                }

                case OBJ_SHIP_ResetSN:
                case OBJ_SHIP_LidarParam:
                case OBJ_SHIP_SailParam:
                case OBJ_SHIP_BackParam:
                case OBJ_SHIP_PathTrackParam:
                case OBJ_SHIP_RunCtrl:
                case OBJ_SHIP_PMSCtrl:
                case OBJ_SHIP_MotorCtrl:
                case OBJ_SHIP_LightCtrl:
                case OBJ_SHIP_BerthCtrl:
                case OBJ_SHIP_ChangeCtrl:
                {
                    break;
                }

                case OBJ_SHIP_StateData:
                {
                    MDATA_SHIP_StateData_t StateData;
                    memcpy(&StateData, pBuf, Len);
                        
                    // 核心动作：将底层原生结构体直接写入抽屉
                    shared_state.WriteShipState(StateData);
                            
                    // 临时打印测试，验证是否写入成功
                    printf("\r---[ship_node] State Updated. Lat: %.6f, Lon: %.6f ", 
                    StateData.navi.latitude, StateData.navi.longitude);
                    break;    
                    }
                default:
                    break;
            }
        }

        // 将控制指令通过 MTLink 发送给底层主控板
        // ---------- 下发 RunCtrl (20Hz, 每50ms下发一次) ----------
        static ULONGLONG last_send_time = 0;
        //Windows毫秒级时间戳函数 GetTickCount64()。
        if (GetTickCount64() - last_send_time >= 50) 
        {
            MDATA_SHIP_RunCtrl_t current_ctrl = shared_state.ReadRunCtrl();
            MTLink_Send(&g_MTLink_ship, 0, 0, OBJ_SHIP_RunCtrl, (uint8_t*)&current_ctrl, sizeof(MDATA_SHIP_RunCtrl_t));
            
            last_send_time = GetTickCount64();
        }

        //下发透传指令 (代替原来的 RawData_sub)
        RawDataPacket raw_packet;
        // 用 while 循环，如果队列里积压了多条指令，一次性全部发出去，防止堵塞
        while (shared_state.PopRawDataDown(raw_packet))
        {
            // raw_packet.payload.data() //可以获取 vector 底层的 uint8_t 数组指针
            // raw_packet.payload.size() //数据长度
            MTLink_Send(&g_MTLink_ship, 0, 0, raw_packet.obj, 
                        raw_packet.payload.data(), 
                        raw_packet.payload.size());
            printf("\r---[ship_node] Sent RawData Obj: 0x%04x \n", raw_packet.obj);
        }

        Sleep(10);
    }

    if (thread_udpListen.joinable()) 
    {
        thread_udpListen.join();
    }
    
    if (thread_tcpcShipMTLinkConnect.joinable()) 
    {
        thread_tcpcShipMTLinkConnect.join();
    }
    
    return 0;
}