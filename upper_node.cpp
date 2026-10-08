#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <csignal>
#include <atomic>
#include <iostream>
#include <sstream>     // tcpsUpperJsonThread 用了 std::istringstream
#include "config/Config.h"  
#include "SharedState.h"    // 引入共享数据

#include <boost/asio.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/filesystem.hpp>

#include <thread>
#include <functional>

#include "upper_node.h"
#include "config_manager_tool/config_manager_tool.h"

extern "C" {
#include "MTLink/MTLink.h"
}


using boost::asio::io_context;
using boost::asio::ip::udp;
using boost::asio::ip::tcp;
using boost::asio::ip::address;
using boost::asio::buffer;
using boost::asio::socket_base;

// static int BROAD_PORT;
// static int DEVICE_ID;
// static int DEVICE_PORT;
// static std::string DEVICE_TYPE;
// static std::string DEVICE_SN;

// static MTLink_t m_MTLink_upper = {0};

// 构造函数：初始化抽屉引用
Upper::Upper(SharedState& state) : state_(state), m_running(nullptr) {
    memset(&m_MTLink_upper, 0, sizeof(MTLink_t));
}

Upper::~Upper() {
    // 如果需要清理资源，写在这里
}

// 对应原来 main() 里的 config 解析部分
bool Upper::Init() {
    Config config; 
    if (!config.Load("../config/config.yaml"))  {
        std::cerr << "读取配置失败: config.yaml\n" ;
        return false;
    }

    BROAD_PORT  = config.Get<int>("upper", "BROAD_PORT", 5000); 
    DEVICE_ID   = config.Get<int>("ship", "DEVICE_ID", 0);
    DEVICE_PORT = config.Get<int>("ship", "DEVICE_PORT", 5001);
    DEVICE_TYPE = config.Get<std::string>("ship", "DEVICE_TYPE", "ship_calc");
    DEVICE_SN   = config.Get<std::string>("ship", "DEVICE_SN", "NULL");

    // 注册 MTLink 回调（注意：将 this 指针作为 context 传给静态回调函数！）
    MTLink_RegSendFunc(&m_MTLink_upper, upperTcpsSendBuffer, this);
    MTLink_RegReciveFunc(&m_MTLink_upper, upperTcpsRecvBuffer, this);

    std::cout << "[Upper] 初始化成功，端口: " << DEVICE_PORT << std::endl;
    return true;
}


// static std::vector<uint8_t> g_json_buffer; // 全局缓冲区用于累积JSON数据

// static std::mutex upper_clients_mutex; 
// static std::vector<std::shared_ptr<tcp::socket>> upper_clients; //上位机客户端连接池
// static std::mutex upper_clients_to_remove_mutex;
// static std::vector<size_t> upper_clients_to_remove; //上位机客户端连接移除标志

// static std::atomic<bool> g_running{true};
// static void signalHandler(int) {
//     g_running = false;
// }

// 声明外部的全局数据 (在 ship_node 和 upper_node 之间共享)
// extern SharedState state_;


// TCPS发送，用于注册MTLink
// 注意：前面加上了 Upper::
bool Upper::upperTcpsSendBuffer(void* context, uint8_t* buf, uint16_t len) 
{
    // 关键！把传进来的 context 强转回 Upper 对象指针
    Upper* self = static_cast<Upper*>(context);
    if (!self) return false;

    bool state = false;
    std::vector<std::shared_ptr<tcp::socket>> current_clients;
        
    {
        // 使用 self-> 访问成员变量
        std::lock_guard<std::mutex> lock(self->m_clients_mutex);
        current_clients = self->m_clients;
    }
        
    for (size_t i = 0; i < current_clients.size(); ++i) 
    {
        auto& socket = current_clients[i];

        if (!socket->is_open()) 
            continue;

        boost::system::error_code ec;
        size_t bytes_sent = socket->write_some(buffer(buf, len), ec);
        state |= (ec ? false : true); 

        if (!ec && bytes_sent  > 0) {
            printf("---[Debug] 5001 端口成功发送数据到上位机，长度: %zu 字节\n", bytes_sent);
        }
    }

    return state;
}

// TCPS接收，用于注册MTLink
bool Upper::upperTcpsRecvBuffer(void* context, uint8_t* buf, uint16_t* len) 
{
    Upper* self = static_cast<Upper*>(context);
    if (!self) return false;

    bool state = false;
    std::vector<std::shared_ptr<tcp::socket>> current_clients;
        
    {
        std::lock_guard<std::mutex> lock(self->m_clients_mutex);
        current_clients = self->m_clients;
    }
        
    static int call_count = 0;
    if (++call_count % 100 == 1) {
        printf("[Recv] called #%d, clients=%zu\n", call_count, current_clients.size());
    }

    for (size_t i = 0; i < current_clients.size(); ++i) 
    {
        auto& socket = current_clients[i];

        if (!socket->is_open()) 
        {
            {
                std::lock_guard<std::mutex> lock(self->m_clients_to_remove_mutex);
                self->m_clients_to_remove.push_back(i);
            }
            continue;
        }

        boost::system::error_code ec;
        size_t bytes_read = socket->read_some(buffer(buf, 2048), ec);
        
        *len = static_cast<uint16_t>(bytes_read);
        state |= (ec ? false : true); 

        if (!ec && bytes_read > 0) {
            printf("[Recv] %zu bytes: ", bytes_read);
            for (size_t k = 0; k < bytes_read; ++k) printf("%02X ", buf[k]);
            printf("\n");
        }

        if (ec && ec != boost::asio::error::would_block)
        {
            printf("---[upper_node] upper Client disconnected\n");
            socket->close();
            {
                std::lock_guard<std::mutex> lock(self->m_clients_to_remove_mutex);
                self->m_clients_to_remove.push_back(i);
            }
        }

        if (state && *len > 0) break;    
    }
    
    return state;
}

//信息广播线程函数   static void udpBroadcastThread()   跟void Upper::udpBroadcastThread()
void Upper::udpBroadcastThread()  
{
    try {
        io_context io_context_; 
        udp::socket socket_(io_context_); //创建套接字
        udp::endpoint endpoint_(udp::v4(), 0); 
        socket_.open(endpoint_.protocol()); //打开套接字

        socket_.set_option(socket_base::broadcast(true)); //允许广播
        socket_.set_option(socket_base::reuse_address(true)); //允许地址重用
        socket_.bind(endpoint_); //绑定套接字

        //获取本地IP
        std::string local_ip = "127.0.0.1";
        socket_.connect(boost::asio::ip::udp::endpoint(boost::asio::ip::address_v4::broadcast(), BROAD_PORT));
        auto local_endpoint = socket_.local_endpoint();
        local_ip = local_endpoint.address().to_string();   
        printf("---[upper_node.cpp] udpBroadcast->local_ip is %s\n", local_ip.c_str());

        // 创建JSON格式的消息
        std::string json_str = "{\"id\":" + std::to_string(DEVICE_ID) + 
                       ",\"ip\":\"" + local_ip + 
                       "\",\"port\":" + std::to_string(DEVICE_PORT) + 
                       ",\"type\":\"" + DEVICE_TYPE + 
                       "\",\"sn\":\"" + DEVICE_SN + "\"}";
        // 创建广播地址
        // while (ros::ok()) 
        while (*m_running)
        {
            try {
                // 发送消息
                size_t bytes_sent = socket_.send(buffer(json_str));
                        
            } catch (const std::exception& e) {
                std::cerr << "upper_node.cpp ==== Error sending broadcast: " << e.what() << std::endl;
            }

            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    } catch (const std::exception& e) {
        std::cerr << "upper_node.cpp ==== Failed to initialize broadcast socket: " << e.what() << std::endl;
        return; // 初始化失败直接返回
    }
}

//TCP服务端接受客户端连接线程，支持多客户端连接
void Upper::tcpsUpperMTLinkAcceptThread()
{   
    printf("[Accept] thread start, port=%d\n", DEVICE_PORT);  
    try {
        io_context io_context_;
        tcp::acceptor acceptor_(io_context_, tcp::endpoint(tcp::v4(), DEVICE_PORT));
        printf("[Accept] listening on port %d\n", DEVICE_PORT);  // ★ 加这行

        // 开始异步接受的函数
        std::function<void()> do_accept;
        do_accept = [&]() 
        {
            auto new_socket = std::make_shared<tcp::socket>(io_context_);
            
            acceptor_.async_accept(*new_socket,
                [&, new_socket](const boost::system::error_code& error) {
                    if (!error) {
                        // 获取客户端信息
                        std::string client_ip = new_socket->remote_endpoint().address().to_string();
                        int client_port = new_socket->remote_endpoint().port();
                        
                        printf("---[upper_node.cpp] upper new connection from %s:%d\n", client_ip.c_str(), client_port);
                        
                        // 设置为非阻塞，方便主循环读取
                        new_socket->non_blocking(true);
                        
                        // 添加到全局客户端列表
                        {
                            std::lock_guard<std::mutex> lock(m_clients_mutex);
                            m_clients.push_back(new_socket);
                        }
                    } 
                    else 
                    {
                        printf("ERROR---[upper_node.cpp] upper accept error: %s\n", error.message().c_str());
                    }
                    
                    // 继续接受下一个连接
                    do_accept();
                });
        };

        // 开始第一次异步接受
        do_accept();
        
        // 运行io_context处理异步操作
        auto work = std::make_shared<io_context::work>(io_context_);
        io_context_.run();
    } catch (const std::exception& e) {
        // 这里通常不会执行，除非初始化失败
        std::cerr << "upper_node.cpp ====  upper server fatal exception: " << e.what() << std::endl;
    }
}


//TCP服务端接受客户端连接，接收解析客户端发送的JSON数据并保存为文件
void Upper::tcpsUpperJsonThread()
{
    try {
        io_context io_context_;
        tcp::acceptor acceptor_(io_context_, tcp::endpoint(tcp::v4(), DEVICE_PORT+1));
        acceptor_.listen(1);

        tcp::socket socket_(io_context_);

        auto do_accept_ptr = std::make_shared<std::function<void()>>();
        *do_accept_ptr = [&, do_accept_ptr]() 
        {
            acceptor_.async_accept(socket_,
                [&, do_accept_ptr](const boost::system::error_code& error) {
                    if (!error)
                    {
                        std::string client_ip = socket_.remote_endpoint().address().to_string();
                        int client_port = socket_.remote_endpoint().port();
                        printf("---[upper_node.cpp] jsonPort connection from %s:%d\n", client_ip.c_str(), client_port);

                        // 关键：创建do_read的智能指针
                        auto do_read_ptr = std::make_shared<std::function<void()>>();
                        *do_read_ptr = [&, do_accept_ptr, do_read_ptr]() 
                        {
                            auto buf = std::make_shared<std::vector<uint8_t>>(65535);
                            socket_.async_read_some(boost::asio::buffer(*buf),
                                [&, buf, do_accept_ptr, do_read_ptr](const boost::system::error_code& ec, size_t bytes_read) {
                                    if (!ec && bytes_read > 0) 
                                    {
                                        // 处理数据
                                        std::string data(buf->begin(), buf->begin() + bytes_read);
                                        printf("---[upper_node.cpp] received json data %zu bytes\r\njson data:  %s\n", bytes_read,  data.c_str());

                                        //包路径的task文件夹，使用纯 C++ 相对路径
                                        std::string task_file_dir = "./task/";
                                        
                                        // 确保目录存在
                                        if (!boost::filesystem::exists(task_file_dir)) 
                                            boost::filesystem::create_directories(task_file_dir);

                                        
                                        // 1. 将新数据追加到缓冲区
                                        m_json_buffer.insert(m_json_buffer.end(), data.begin(), data.end());
                                        
                                        // 2. 转换为字符串进行处理
                                        std::string json_str(m_json_buffer.begin(), m_json_buffer.end());
                                        
                                        // 3. 使用更可靠的JSON完整性检测
                                        size_t open_brace = json_str.find('{');
                                        if (open_brace == std::string::npos) 
                                        {
                                            // 没有找到起始大括号，清空缓冲区
                                            m_json_buffer.clear();
                                            return;
                                        }
                                        
                                        // 从第一个大括号开始处理
                                        json_str = json_str.substr(open_brace);
                                        m_json_buffer.erase(m_json_buffer.begin(), m_json_buffer.begin() + open_brace);
                                        
                                        // 4. 使用栈来匹配大括号，检测完整的JSON
                                        int brace_count = 0;
                                        size_t complete_end = 0;
                                        
                                        for (size_t i = 0; i < json_str.size(); ++i) 
                                        {
                                            if (json_str[i] == '{') 
                                            {
                                                brace_count++;
                                            } 
                                            else if (json_str[i] == '}') 
                                            {
                                                brace_count--;
                                                if (brace_count == 0) 
                                                {
                                                    complete_end = i + 1;
                                                    break;
                                                }
                                            }
                                        }
                                        
                                        // 5. 如果找到完整的JSON对象
                                        if (brace_count == 0 && complete_end > 0) 
                                        {
                                            std::string complete_json = json_str.substr(0, complete_end);
                                            
                                            try 
                                            {
                                                // 验证JSON格式
                                                std::istringstream iss(complete_json);
                                                boost::property_tree::ptree pt;
                                                boost::property_tree::read_json(iss, pt);
                                                
                                                // 保存
                                                std::string filename = task_file_dir + "task_file" + ".json";
                                                std::ofstream file(filename);
                                                
                                                if (file.is_open()) 
                                                {
                                                    boost::property_tree::write_json(file, pt, true);
                                                    file.close();
                                                    printf("---[upper_node.cpp] json file saved to %s\n", filename.c_str());

                                                    // 触发共享标志位，通知算法线程去读新任务
                                                    state_.WriteNewTaskFlag(true); 
                                                    
                                                    m_json_buffer.clear();
                                                } 
                                                else 
                                                {
                                                    printf("ERROR---[upper_node.cpp] json file open failed: %s\n", filename.c_str());
                                                }
                                            }
                                            catch (const std::exception& e) 
                                            {
                                                std::cerr << "upper_node.cpp ====  json processing error: " << e.what() << std::endl;
                                            }
                                        }
                                        
                                        // 6. 防止缓冲区无限增长
                                        if (m_json_buffer.size() > 1024 * 1024)// 1MB限制
                                        { 
                                            printf("WARNING---[upper_node.cpp] json buffer too large, clearing to prevent memory issues\n");
                                            m_json_buffer.clear();
                                        }
                                        
                                        // 继续读取下一包
                                        (*do_read_ptr)();
                                    }
                                    else
                                    {
                                        printf("---[upper_node.cpp] jsonPort connection ended\n");
                                        m_json_buffer.clear();
                                        socket_.close();

                                        (*do_accept_ptr)();
                                    }
                                });
                        };

                        (*do_read_ptr)();
                    }
                    else 
                    {
                        printf("ERROR---[upper_node.cpp] jsonPort accept error: %s\n", error.message().c_str());
                        
                        socket_.close();
                        (*do_accept_ptr)();
                    }
                });
        };
        

        (*do_accept_ptr)();
        io_context_.run();

    } catch (const std::exception& e) {
        std::cerr << "upper_node.cpp ====  json server fatal error: " << e.what() << std::endl;
    }
}

void Upper::Run(std::atomic<bool>* running) 
{   
    // 保存外部传进来的运行标志（给内部子线程用）
    m_running = running;
    // Config config; 
    // if (!config.Load("common/Config/config.yaml")) {
    //     std::cerr << "读取配置失败: Config/config.yaml\n" ;
    //     return 1;
    // }

    // BROAD_PORT  = config.Get<int>("upper", "BROAD_PORT", 5000); 
    // DEVICE_ID   = config.Get<int>("ship", "DEVICE_ID", 0);
    // DEVICE_PORT = config.Get<int>("ship", "DEVICE_PORT", 5001);
    // DEVICE_TYPE = config.Get<std::string>("ship", "DEVICE_TYPE", "ship_calc");
    // DEVICE_SN   = config.Get<std::string>("ship", "DEVICE_SN", "NULL");

    // std::signal(SIGINT, signalHandler);
    // std::signal(SIGTERM, signalHandler);

    //UDP广播
    std::thread thread_udpBroadcast(&Upper::udpBroadcastThread, this);//启动线程
    //TCP服务端MTLink
    std::thread thread_tcpsUpperMTLinkAccept(&Upper::tcpsUpperMTLinkAcceptThread, this);//启动线程
    //TCP服务端TaskFile
    std::thread thread_tcpsUpperJson(&Upper::tcpsUpperJsonThread, this);//启动线程

    // memset(&m_MTLink_upper, 0, sizeof(MTLink_t));
    // MTLink_RegSendFunc(&m_MTLink_upper, upperTcpsSendBuffer, NULL);
    // MTLink_RegReciveFunc(&m_MTLink_upper, upperTcpsRecvBuffer, NULL);

     while (*m_running)
    {
        uint8_t SID;
		uint8_t DID;
		uint16_t Obj;
		uint8_t *pBuf;
		uint16_t Len;

        if(MTLink_Receive(&m_MTLink_upper, &SID, &DID, &Obj, &pBuf, &Len))
        {
            switch(Obj)
			{
                case OBJ_SHIP_BoardReset:
                case OBJ_SHIP_BoardInfo:
                case OBJ_SHIP_BoardVersion:
                case OBJ_SHIP_BoardNetwork:
                {
                    printf("---[upper_node.cpp]  publish raw obj is 0x%04x:\n",Obj);
                    //工控机不处理以上指令，不做数据解析，将原始数据直接发布给主控（透传）
                    state_.PushRawDataDown(Obj, pBuf, Len);
                
                    break;
                }

                case OBJ_SHIP_SetID:
                {
                    //ID参数的处理，支持工控机端保存，并将指令同步转发给主板
                    MDATA_SHIP_ID_t SHIP_ID;
                    memcpy(&SHIP_ID, pBuf, Len);

                    if(SHIP_ID.flag == 1)
                    {
                        //将yaml文件中的DEVICE_SN修改为本次接收到的sn并保存
                        std::string config_file  = "./config/config.yaml";
                        ConfigManagerTool::updateParamInFile(config_file, "DEVICE_ID", SHIP_ID.ID);//更新参数
                    }

                    state_.WriteShipID(SHIP_ID);
                    break;
                }

                case OBJ_SHIP_BoardPort:
                case OBJ_SHIP_YawPIDParam:
                case OBJ_SHIP_SpeedPIDParam:
                case OBJ_SHIP_DisPIDParam:
                {
                    //工控机不处理以上指令，不做数据解析，将原始数据直接发布给主控（透传）
                    state_.PushRawDataDown(Obj, pBuf, Len);

                    break;
                }

                case OBJ_SHIP_ResetSN:
                {
                    std::string config_file  = "./config/config.yaml";

                    ConfigManagerTool::updateParamInFile(config_file, "DEVICE_SN", "NULL");
                    MTLink_Send(&m_MTLink_upper, 0, 0, OBJ_SHIP_ResetSN, NULL, 0);
                    break;
                }

                case OBJ_SHIP_LidarParam:
                {
                    MDATA_SHIP_LidarParam_t SHIP_Lidar;
                    memcpy(&SHIP_Lidar, pBuf, Len);

                    if(SHIP_Lidar.flag == 0x01)
                    {
                        // usvc_controller::UpdateParams srv;
                        // srv.request.node_name = "sensor";

                        // srv.request.param_names = {
                        //     "X_DISTANCE", 
                        //     "Y_DISTANCE", 
                        //     "Z_DISTANCE",
                        //     "ClusterTolerance",
                        //     "MinClusterSize",
                        //     "MaxClusterSize"
                        // };

                        // srv.request.param_values = {
                        //     std::to_string(SHIP_Lidar.X_DISTANCE),
                        //     std::to_string(SHIP_Lidar.Y_DISTANCE),
                        //     std::to_string(SHIP_Lidar.Z_DISTANCE),
                        //     std::to_string(SHIP_Lidar.ClusterTolerance),
                        //     std::to_string(SHIP_Lidar.MinClusterSize),
                        //     std::to_string(SHIP_Lidar.MaxClusterSize)
                        // };

                        // lidar_param_client.call(srv);

                        std::string config_file  ="./config/config.yaml";

                        ConfigManagerTool::updateParamInFile(config_file, "sensor/lidar/X_DISTANCE", SHIP_Lidar.X_DISTANCE);
                        ConfigManagerTool::updateParamInFile(config_file, "sensor/lidar/Y_DISTANCE", SHIP_Lidar.Y_DISTANCE);
                        ConfigManagerTool::updateParamInFile(config_file, "sensor/lidar/Z_DISTANCE", SHIP_Lidar.Z_DISTANCE);
                        ConfigManagerTool::updateParamInFile(config_file, "sensor/lidar/ClusterTolerance", SHIP_Lidar.ClusterTolerance);
                        ConfigManagerTool::updateParamInFile(config_file, "sensor/lidar/MinClusterSize", SHIP_Lidar.MinClusterSize);
                        ConfigManagerTool::updateParamInFile(config_file, "sensor/lidar/MaxClusterSize", SHIP_Lidar.MaxClusterSize);

                        SHIP_Lidar.flag = 0x11;
                        MTLink_Send(&m_MTLink_upper, 0, 0, OBJ_SHIP_LidarParam, (uint8_t*)&SHIP_Lidar, sizeof(MDATA_SHIP_LidarParam_t));
                    }
                    else if(SHIP_Lidar.flag == 0x03)
                    {
                        std::string config_file  ="./config/config.yaml";

                        ConfigManagerTool::getParamFromFile(config_file, "sensor/lidar/X_DISTANCE", SHIP_Lidar.X_DISTANCE);
                        ConfigManagerTool::getParamFromFile(config_file, "sensor/lidar/Y_DISTANCE", SHIP_Lidar.Y_DISTANCE);
                        ConfigManagerTool::getParamFromFile(config_file, "sensor/lidar/Z_DISTANCE", SHIP_Lidar.Z_DISTANCE);
                        ConfigManagerTool::getParamFromFile(config_file, "sensor/lidar/ClusterTolerance", SHIP_Lidar.ClusterTolerance);
                        ConfigManagerTool::getParamFromFile(config_file, "sensor/lidar/MinClusterSize", SHIP_Lidar.MinClusterSize);
                        ConfigManagerTool::getParamFromFile(config_file, "sensor/lidar/MaxClusterSize", SHIP_Lidar.MaxClusterSize);
                    
                        SHIP_Lidar.flag = 0x31;
                        MTLink_Send(&m_MTLink_upper, 0, 0, OBJ_SHIP_LidarParam, (uint8_t*)&SHIP_Lidar, sizeof(MDATA_SHIP_LidarParam_t));
                    }
                    else
                    {
                        ;
                    }

                    break;
                }

                case OBJ_SHIP_SailParam:
                {
                    MDATA_SHIP_SailParam_t SHIP_Sail;
                    memcpy(&SHIP_Sail, pBuf, Len);

                    if(SHIP_Sail.flag == 0x01)
                    {
                        std::string config_file  = "./config/config.yaml";
                    
                        ConfigManagerTool::updateParamInFile(config_file, "control/sail/SPEED", SHIP_Sail.SPEED);
                        ConfigManagerTool::updateParamInFile(config_file, "control/sail/Y", SHIP_Sail.Y);
                        ConfigManagerTool::updateParamInFile(config_file, "control/sail/TOLERANCERADIUS1", SHIP_Sail.TOLERANCERADIUS[0]);
                        ConfigManagerTool::updateParamInFile(config_file, "control/sail/TOLERANCERADIUS2", SHIP_Sail.TOLERANCERADIUS[1]);
                        ConfigManagerTool::updateParamInFile(config_file, "control/sail/LAT", SHIP_Sail.POS[0]);
                        ConfigManagerTool::updateParamInFile(config_file, "control/sail/LON", SHIP_Sail.POS[1]);

                        SHIP_Sail.flag = 0x11;
                        MTLink_Send(&m_MTLink_upper, 0, 0, OBJ_SHIP_SailParam, (uint8_t*)&SHIP_Sail, sizeof(MDATA_SHIP_SailParam_t));
                    }
                    else if(SHIP_Sail.flag == 0x03)
                    {
                        std::string config_file  = "./config/config.yaml";

                        ConfigManagerTool::getParamFromFile(config_file, "control/sail/SPEED", SHIP_Sail.SPEED);
                        ConfigManagerTool::getParamFromFile(config_file, "control/sail/Y", SHIP_Sail.Y);
                        ConfigManagerTool::getParamFromFile(config_file, "control/sail/TOLERANCERADIUS1", SHIP_Sail.TOLERANCERADIUS[0]);
                        ConfigManagerTool::getParamFromFile(config_file, "control/sail/TOLERANCERADIUS2", SHIP_Sail.TOLERANCERADIUS[1]);
                        ConfigManagerTool::getParamFromFile(config_file, "control/sail/LAT", SHIP_Sail.POS[0]);
                        ConfigManagerTool::getParamFromFile(config_file, "control/sail/LON", SHIP_Sail.POS[1]);

                        SHIP_Sail.flag = 0x31;
                        MTLink_Send(&m_MTLink_upper, 0, 0, OBJ_SHIP_SailParam, (uint8_t*)&SHIP_Sail, sizeof(MDATA_SHIP_SailParam_t));
                    }
                    else
                    {
                        ;
                    }
                    break;
                }

                case OBJ_SHIP_BackParam:
                {
                    MDATA_SHIP_BackParam_t SHIP_Back;
                    memcpy(&SHIP_Back, pBuf, Len);

                    if(SHIP_Back.flag == 0x01)
                    {
                        std::string config_file  = "./config/config.yaml";
                    
                        ConfigManagerTool::updateParamInFile(config_file, "control/back/MAXSPEED", SHIP_Back.MAXSPEED);
                        ConfigManagerTool::updateParamInFile(config_file, "control/back/MINSPEED", SHIP_Back.MINSPEED);
                        ConfigManagerTool::updateParamInFile(config_file, "control/back/LOSDISTANCE", SHIP_Back.LOSDISTANCE);
                        ConfigManagerTool::updateParamInFile(config_file, "control/back/OBSTACLE_INFLUENCE_DISTANCE", SHIP_Back.OBSTACLE_INFLUENCE_DISTANCE);
                        ConfigManagerTool::updateParamInFile(config_file, "control/back/SAFE_DISTANCE", SHIP_Back.SAFE_DISTANCE);
                        ConfigManagerTool::updateParamInFile(config_file, "control/back/REPULSIVE_GAIN", SHIP_Back.REPULSIVE_GAIN);
                        ConfigManagerTool::updateParamInFile(config_file, "control/back/ATTRACTIVE_GAIN", SHIP_Back.ATTRACTIVE_GAIN);
                        ConfigManagerTool::updateParamInFile(config_file, "control/back/SIGMA", SHIP_Back.SIGMA);
                        ConfigManagerTool::updateParamInFile(config_file, "control/back/TOLERANCERADIUS", SHIP_Back.TOLERANCERADIUS);
                        ConfigManagerTool::updateParamInFile(config_file, "control/back/Y", SHIP_Back.Y);
                        ConfigManagerTool::updateParamInFile(config_file, "control/back/LAT", SHIP_Back.POS[0]);
                        ConfigManagerTool::updateParamInFile(config_file, "control/back/LON", SHIP_Back.POS[1]);

                        SHIP_Back.flag = 0x11;
                        MTLink_Send(&m_MTLink_upper, 0, 0, OBJ_SHIP_BackParam, (uint8_t*)&SHIP_Back, sizeof(MDATA_SHIP_BackParam_t));
                    }
                    else if(SHIP_Back.flag == 0x03)
                    {
                        std::string config_file  = "./config/config.yaml";

                        ConfigManagerTool::getParamFromFile(config_file, "control/back/MAXSPEED", SHIP_Back.MAXSPEED);
                        ConfigManagerTool::getParamFromFile(config_file, "control/back/MINSPEED", SHIP_Back.MINSPEED);
                        ConfigManagerTool::getParamFromFile(config_file, "control/back/LOSDISTANCE", SHIP_Back.LOSDISTANCE);
                        ConfigManagerTool::getParamFromFile(config_file, "control/back/OBSTACLE_INFLUENCE_DISTANCE", SHIP_Back.OBSTACLE_INFLUENCE_DISTANCE);
                        ConfigManagerTool::getParamFromFile(config_file, "control/back/SAFE_DISTANCE", SHIP_Back.SAFE_DISTANCE);
                        ConfigManagerTool::getParamFromFile(config_file, "control/back/REPULSIVE_GAIN", SHIP_Back.REPULSIVE_GAIN);
                        ConfigManagerTool::getParamFromFile(config_file, "control/back/ATTRACTIVE_GAIN", SHIP_Back.ATTRACTIVE_GAIN);
                        ConfigManagerTool::getParamFromFile(config_file, "control/back/SIGMA", SHIP_Back.SIGMA);
                        ConfigManagerTool::getParamFromFile(config_file, "control/back/TOLERANCERADIUS", SHIP_Back.TOLERANCERADIUS);
                        ConfigManagerTool::getParamFromFile(config_file, "control/back/Y", SHIP_Back.Y);
                        ConfigManagerTool::getParamFromFile(config_file, "control/back/LAT", SHIP_Back.POS[0]);
                        ConfigManagerTool::getParamFromFile(config_file, "control/back/LON", SHIP_Back.POS[1]);

                        SHIP_Back.flag = 0x31;
                        MTLink_Send(&m_MTLink_upper, 0, 0, OBJ_SHIP_BackParam, (uint8_t*)&SHIP_Back, sizeof(MDATA_SHIP_BackParam_t));
                    }
                    else
                    {
                        ;
                    }
                    break;
                }

                case OBJ_SHIP_PathTrackParam:
                {
                    MDATA_SHIP_PathTrackParam_t SHIP_PathTrack;
                    memcpy(&SHIP_PathTrack, pBuf, Len);

                    if(SHIP_PathTrack.flag == 0x01)
                    {
                        std::string config_file  = "./config/config.yaml";
                    
                        ConfigManagerTool::updateParamInFile(config_file, "control/path/MAXSPEED", SHIP_PathTrack.MAXSPEED);
                        ConfigManagerTool::updateParamInFile(config_file, "control/path/MINSPEED", SHIP_PathTrack.MINSPEED);
                        ConfigManagerTool::updateParamInFile(config_file, "control/path/LOSDISTANCE", SHIP_PathTrack.LOSDISTANCE);
                        ConfigManagerTool::updateParamInFile(config_file, "control/path/OBSTACLE_INFLUENCE_DISTANCE", SHIP_PathTrack.OBSTACLE_INFLUENCE_DISTANCE);
                        ConfigManagerTool::updateParamInFile(config_file, "control/path/SAFE_DISTANCE", SHIP_PathTrack.SAFE_DISTANCE);
                        ConfigManagerTool::updateParamInFile(config_file, "control/path/REPULSIVE_GAIN", SHIP_PathTrack.REPULSIVE_GAIN);
                        ConfigManagerTool::updateParamInFile(config_file, "control/path/ATTRACTIVE_GAIN", SHIP_PathTrack.ATTRACTIVE_GAIN);
                        ConfigManagerTool::updateParamInFile(config_file, "control/path/SIGMA", SHIP_PathTrack.SIGMA);
                        ConfigManagerTool::updateParamInFile(config_file, "control/path/TOLERANCERADIUS", SHIP_PathTrack.TOLERANCERADIUS);

                        SHIP_PathTrack.flag = 0x11;
                        MTLink_Send(&m_MTLink_upper, 0, 0, OBJ_SHIP_PathTrackParam, (uint8_t*)&SHIP_PathTrack, sizeof(MDATA_SHIP_PathTrackParam_t));
                    }
                    else if(SHIP_PathTrack.flag == 0x03)
                    {
                        std::string config_file  = "./config/config.yaml";

                        ConfigManagerTool::getParamFromFile(config_file, "control/path/MAXSPEED", SHIP_PathTrack.MAXSPEED);
                        ConfigManagerTool::getParamFromFile(config_file, "control/path/MINSPEED", SHIP_PathTrack.MINSPEED);
                        ConfigManagerTool::getParamFromFile(config_file, "control/path/LOSDISTANCE", SHIP_PathTrack.LOSDISTANCE);
                        ConfigManagerTool::getParamFromFile(config_file, "control/path/OBSTACLE_INFLUENCE_DISTANCE", SHIP_PathTrack.OBSTACLE_INFLUENCE_DISTANCE);
                        ConfigManagerTool::getParamFromFile(config_file, "control/path/SAFE_DISTANCE", SHIP_PathTrack.SAFE_DISTANCE);
                        ConfigManagerTool::getParamFromFile(config_file, "control/path/REPULSIVE_GAIN", SHIP_PathTrack.REPULSIVE_GAIN);
                        ConfigManagerTool::getParamFromFile(config_file, "control/path/ATTRACTIVE_GAIN", SHIP_PathTrack.ATTRACTIVE_GAIN);
                        ConfigManagerTool::getParamFromFile(config_file, "control/path/SIGMA", SHIP_PathTrack.SIGMA);
                        ConfigManagerTool::getParamFromFile(config_file, "control/path/TOLERANCERADIUS", SHIP_PathTrack.TOLERANCERADIUS);


                        SHIP_PathTrack.flag = 0x31;
                        MTLink_Send(&m_MTLink_upper, 0, 0, OBJ_SHIP_PathTrackParam, (uint8_t*)&SHIP_PathTrack, sizeof(MDATA_SHIP_PathTrackParam_t));
                    }
                    else
                    {
                        ;
                    }
                    break;
                }

                case OBJ_SHIP_RunCtrl:
                {
                    MDATA_SHIP_RunCtrl_t SHIP_RunCtrl;
                    memcpy(&SHIP_RunCtrl, pBuf, Len);
                    state_.WriteRunCtrl(SHIP_RunCtrl);
                    
                    break;
                }

                case OBJ_SHIP_PMSCtrl:
                case OBJ_SHIP_MotorCtrl:
                case OBJ_SHIP_LightCtrl:
                case OBJ_SHIP_BerthCtrl:
                case OBJ_SHIP_ChangeCtrl:
                { 
                    //工控机不处理以上指令，不做数据解析，将原始数据直接发布给主控（透传）
                    printf("---[upper_node.cpp]  publish raw obj is 0x%04x:\n",Obj);
                    state_.PushRawDataDown(Obj, pBuf, Len);

                    break;
                }
                default:
                    break;
            }
        }
        // 10Hz 快速刷新通道 (每 100ms 发送一次)，底板状态数据 (StateData)、雷达数据 (LidarData)
        static ULONGLONG last_fast_send_time = 0;
        if (GetTickCount64() - last_fast_send_time >= 100) 
        {
            // 1. 发送底板状态数据
            MDATA_SHIP_StateData_t current_state = state_.ReadShipState();
            // 如果经纬度都不是0，说明底层数据有效，上报给 UI
            if (current_state.navi.latitude != 0.0) {
                MTLink_Send(&m_MTLink_upper, 0, 0, OBJ_SHIP_StateData, 
                            (uint8_t*)&current_state, sizeof(MDATA_SHIP_StateData_t));
            }
            
            // 2. 发送雷达数据 (补充了发送逻辑)
            MDATA_SHIP_LidarData_t lidar_data = state_.ReadPointCloud();
            // 只要雷达数据里有有效的点数，就发给 UI
            if (lidar_data.nums > 0) {
                MTLink_Send(&m_MTLink_upper, 0, 0, OBJ_SHIP_LidarData, 
                            (uint8_t*)&lidar_data, sizeof(MDATA_SHIP_LidarData_t));
            }
            
            last_fast_send_time = GetTickCount64();
        }
        // 1Hz 慢速刷新通道 (每 1000ms 发送一次)，累计里程数据 (ACHData)、设备 ID (ID)
        static ULONGLONG last_slow_send_time = 0;
        if (GetTickCount64() - last_slow_send_time >= 1000) 
        {
            // 1. 发送累计里程数据
            MDATA_SHIP_ACHData_t current_ach = state_.ReadACHData();
            if (current_ach.total_operating_hours > 0 || current_ach.total_distance > 0) {
                MTLink_Send(&m_MTLink_upper, 0, 0, OBJ_SHIP_ACHData, 
                            (uint8_t*)&current_ach, sizeof(MDATA_SHIP_ACHData_t));
            }

            // 2. 发送设备 ID 数据
            MDATA_SHIP_ID_t current_id = state_.ReadID();
            if (current_id.ID != 0) {
                MTLink_Send(&m_MTLink_upper, 0, 0, OBJ_SHIP_SetID, 
                            (uint8_t*)&current_id, sizeof(MDATA_SHIP_ID_t));
            }
            
            last_slow_send_time = GetTickCount64();
        }
        /******************************************************************************************/
        /******************************************************************************************/
        // 清理断开连接的客户端
        std::vector<size_t> to_remove;
        {
            std::lock_guard<std::mutex> lock(m_clients_to_remove_mutex);
            to_remove = m_clients_to_remove;
            m_clients_to_remove.clear();  // 清空
        }

        if (!to_remove.empty()) 
        {
            std::lock_guard<std::mutex> lock(m_clients_mutex);
            
            // 从后往前删除
            for (auto it = to_remove.rbegin(); it != to_remove.rend(); ++it) 
            {
                if (*it < m_clients.size()) 
                {
                    m_clients.erase(m_clients.begin() + *it);
                }
            }
        }

        Sleep(10);
    }

    
    if (thread_udpBroadcast.joinable()) {
        thread_udpBroadcast.join();
    }
    
    if (thread_tcpsUpperMTLinkAccept.joinable()) {
        thread_tcpsUpperMTLinkAccept.detach();
    }
    
    if (thread_tcpsUpperJson.joinable()) {
        thread_tcpsUpperJson.detach();
    }
}