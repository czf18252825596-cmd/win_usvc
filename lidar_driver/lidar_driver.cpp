#include "livox_driver.h"
#include <Eigen/Dense>      // 矩阵计算
#include <cmath>            //  std::sqrt
#include <algorithm>        //  std::sort
#include <vector>           //  std::vector

LivoxDriver::LivoxDriver(SharedState& shared_state) 
    : shared_state_(shared_state) // 记住外面传进来的抽屉
{
}

LivoxDriver::~LivoxDriver() {
    // 退出时关闭 SDK
    LivoxLidarSdkUninit();
}

bool LivoxDriver::Init(const std::string& config_path) {
    // 1. 初始化 SDK (传入配置文件，这个 json 是 Livox 官方自带的格式)
    if (!LivoxLidarSdkInit(config_path.c_str())) {
        std::cerr << "Livox SDK Init Failed!" << std::endl;
        return false;
    }

    // 2. 注册点云回调函数
    // 关键看最后一个参数：把当前对象的指针 (this) 作为 client_data 传给 SDK！
    SetLivoxLidarPointCloudCallBack(PointCloudCallback, this);

    std::cout << "Livox Lidar Init Success & Callback Registered." << std::endl;
    return true;
}

// 3. 点云回调函数 (静态函数)
void LivoxDriver::PointCloudCallback(uint32_t handle, 
                                     const uint8_t dev_type, 
                                     LivoxLidarEthernetPacket* data, 
                                     void* client_data) 
{
    // 这个函数被雷达触发了！但是它是 static 的，拿不到成员变量。
    //  client_data，强制转换回 LivoxDriver 对象
    LivoxDriver* driver = static_cast<LivoxDriver*>(client_data);
    if (data = nullptr || driver =nullptr )   return;
    //是不是属于高精度笛卡尔坐标系类型
    if (data->data_type != kLivoxLidarCartesianCoordinateHighData) {
        // 数据类型  高精度笛卡尔坐标
        // typedef struct {
        //   int32_t x;            /**< X axis, Unit:mm */
        //   int32_t y;            /**< Y axis, Unit:mm */
        //   int32_t z;            /**< Z axis, Unit:mm */
        //   uint8_t reflectivity; /**< Reflectivity */反射强度
        //   uint8_t tag;          /**< Tag */
        // } LivoxLidarCartesianHighRawPoint;
        return; 
    }
    LivoxLidarCartesianHighRawPoint* points = 
        reinterpret_cast<LivoxLidarCartesianHighRawPoint*>(data->data)//强制类型转换
    //LivoxLidarEthernetPacket结构体      LivoxLidarCartesianHighRawPoint//高精度笛卡尔坐标对应的结构体
    uint32_t point_num = (data->length - sizeof(LivoxLidarEthernetPacket)) 
                       / sizeof(LivoxLidarCartesianHighRawPoint);

    static std::vector<std::pair<float, Eigen::Vector3f>> point_cloud_buffer;
    point_cloud_buffer.reserve(point_num);

    static uint64_t last_time = 0;
    uint64_t current_time = GetTickCount64();
    if(last_time == 0)   last_time = current_time;

    for (uint32_t i = 0; i < point_num; ++i)
    {   
        // 1. 提取原始数据，并把 毫米转换成 米
        float x_m = points[i].x / 1000.0f;
        float y_m = points[i].y / 1000.0f;
        float z_m = points[i].z / 1000.0f;
        Eigen::Vector3f p(x_m, y_m, z_m);
        // 计算到原点的距离（欧氏距离）
        float distance = std::sqrt(x_m * x_m + y_m * y_m + z_m * z_m);
        point_cloud_buffer.emplace_back(distance, p);
    }

    if (current_time - last_time < 100)  // 雷达频率是10Hz，每10帧处理一次就是1秒
    {   
        return;    
    }
    if (point_cloud_buffer.empty()) {
        last_time = current_time;
        return;
    }

    // 按照距离排序（从小到大）
    std::sort(point_cloud_buffer.begin(), point_cloud_buffer.end(), 
              [](const std::pair<float, Eigen::Vector3f>& a,
                const std::pair<float, Eigen::Vector3f>& b){return a.first < b.first;
              });
    
    MDATA_SHIP_LidarData_t LidarData;
    int num_points = point_cloud_buffer.size();
    LidarData.nums = (num_points > 100) ? 100 : static_cast<uint8_t>(num_points);
    
    for (int i = 0; i < LidarData.nums; ++i) 
    {
        LidarData.x[i] = point_cloud_buffer[i].second.x();
        LidarData.y[i] = point_cloud_buffer[i].second.y();
    }

    // 4. 发送给大抽屉
    driver->shared_state_.WriteLidarData(LidarData);

    // 5. 结算与清场：更新时间戳，清空大碗
    last_time = current_time;
    point_cloud_buffer.clear();

    std::cout << "收到雷达数据，帧类型：" << (int)data->data_type << std::endl;
}

