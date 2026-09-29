#define NOMINMAX   
#include <Eigen/Dense>      // 矩阵计算
#include <cmath>            //  std::sqrt
#include <algorithm>        //  std::sort
#include <vector>           //  std::vector
#include "lidar_driver.h"
#include "SharedState.h"
#include "config_manager_tool/config_manager_tool.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/filters/conditional_removal.h>   // ConditionalRemoval、ConditionOr、ConditionAnd、FieldComparison
#include <pcl/filters/approximate_voxel_grid.h>            // VoxelGrid
#include <pcl/search/kdtree.h>
#include <pcl/segmentation/extract_clusters.h>


static float X_DISTANCE = 0.01;   
static float Y_DISTANCE = 0.01;  
static float Z_DISTANCE = 0.5;  
static float ClusterTolerance = 0.1;    // 聚类半径
static int MinClusterSize = 2;          // 最小聚类大小，小于则丢弃(噪点)
static int MaxClusterSize = 200000;     // 最大聚类大小，超过则丢弃
static const std::string YAML_CONFIG_PATH = "./config/config.yaml";

static void UpdateLidarParams() 
{
    // 利用 ConfigManagerTool 直接读取 YAML 文件里的字段
    // 参数一：文件路径
    // 参数二：YAML 中的键路径 (比如 "sensor/lidar/X_DISTANCE")
    // 参数三：你要赋值的内存变量引用
    ConfigManagerTool::getParamFromFile(YAML_CONFIG_PATH, "sensor/lidar/X_DISTANCE", X_DISTANCE);
    ConfigManagerTool::getParamFromFile(YAML_CONFIG_PATH, "sensor/lidar/Y_DISTANCE", Y_DISTANCE);
    ConfigManagerTool::getParamFromFile(YAML_CONFIG_PATH, "sensor/lidar/Z_DISTANCE", Z_DISTANCE);
    
    // ConfigManagerTool 支持重载，可以自动解析 float/double 和 int
    ConfigManagerTool::getParamFromFile(YAML_CONFIG_PATH, "sensor/lidar/ClusterTolerance", ClusterTolerance);
    ConfigManagerTool::getParamFromFile(YAML_CONFIG_PATH, "sensor/lidar/MinClusterSize", MinClusterSize);
    ConfigManagerTool::getParamFromFile(YAML_CONFIG_PATH, "sensor/lidar/MaxClusterSize", MaxClusterSize);

    // 打印出来看看是不是真的读到了
    std::cout << "[LidarDriver] 参数已更新: "
              << "X=" << X_DISTANCE << ", Y=" << Y_DISTANCE << ", Z=" << Z_DISTANCE 
              << ", Tol=" << ClusterTolerance << ", Min=" << MinClusterSize << std::endl;
}

LivoxDriver::LivoxDriver(SharedState& shared_state) 
    : shared_state_(shared_state) // 记住外面传进来的
{
}

LivoxDriver::~LivoxDriver() {
    // 退出时关闭 SDK
    LivoxLidarSdkUninit();
}

bool LivoxDriver::Init(const std::string& config_path) {
    UpdateLidarParams();//更新预处理参数
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
    //  client_data，强制转换回 LivoxDriver 对象
    LivoxDriver* driver = static_cast<LivoxDriver*>(client_data);
    if (data == nullptr || driver ==nullptr )   return;
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
        reinterpret_cast<LivoxLidarCartesianHighRawPoint*>(data->data);//强制类型转换
    //LivoxLidarEthernetPacket结构体      LivoxLidarCartesianHighRawPoint//高精度笛卡尔坐标对应的结构体
    uint32_t point_num = (data->length - sizeof(LivoxLidarEthernetPacket)) 
                       / sizeof(LivoxLidarCartesianHighRawPoint);


    static std::vector<std::pair<float, Eigen::Vector3f>> point_cloud_buffer;
    point_cloud_buffer.reserve(point_num);

    static uint64_t last_time = 0;
    uint64_t current_time = GetTickCount64();
    if(last_time == 0)   last_time = current_time;
    //遍历雷达原始点，提取 x/y/z，算距离，塞进缓冲point_cloud_buffer
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

    UpdateLidarParams();

    pcl::PointCloud<pcl::PointXYZ>::Ptr raw_cloud(new pcl::PointCloud<pcl::PointXYZ>);

    // 2. 遍历缓冲里的点，转成 PCL 格式
    for (const auto& item : point_cloud_buffer) 
    {
        pcl::PointXYZ pcl_point;
        pcl_point.x = item.second.x();
        pcl_point.y = item.second.y();
        pcl_point.z = item.second.z();
        
        raw_cloud->points.push_back(pcl_point);
    }

    // 设置 PCL 点云的宽高属性
    raw_cloud->width = raw_cloud->points.size();
    raw_cloud->height = 1;
    raw_cloud->is_dense = false;

    pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_filte(new pcl::PointCloud<pcl::PointXYZ>); //点云类型的指针
    pcl::ConditionOr<pcl::PointXYZ>::Ptr xy_cond(new pcl::ConditionOr<pcl::PointXYZ>()); //或类型的对象
    // 条件1：-X_DISTANCE ≤ x ≤ X_DISTANCE
    xy_cond->addComparison(//添加比较规则
        pcl::FieldComparison<pcl::PointXYZ>::ConstPtr(
            new pcl::FieldComparison<pcl::PointXYZ>("x", pcl::ComparisonOps::LT, -1 * X_DISTANCE)));  //  LT <
    xy_cond->addComparison(
        pcl::FieldComparison<pcl::PointXYZ>::ConstPtr(
            new pcl::FieldComparison<pcl::PointXYZ>("x", pcl::ComparisonOps::GT, 1 * X_DISTANCE)));   //  GT >
    // 条件2：-Y_DISTANCE ≤ y ≤ Y_DISTANCE
    xy_cond->addComparison(
        pcl::FieldComparison<pcl::PointXYZ>::ConstPtr(
            new pcl::FieldComparison<pcl::PointXYZ>("y", pcl::ComparisonOps::LT, -1 * Y_DISTANCE)));  //  LT <
    xy_cond->addComparison(
        pcl::FieldComparison<pcl::PointXYZ>::ConstPtr(
            new pcl::FieldComparison<pcl::PointXYZ>("y", pcl::ComparisonOps::GT, 1 * Y_DISTANCE)));   //  GT >
    pcl::ConditionAnd<pcl::PointXYZ>::Ptr range_cond(new pcl::ConditionAnd<pcl::PointXYZ>());
    range_cond->addCondition(xy_cond); //加一个已有的条件对象    xy_cond与逻辑
    // 条件3保留z<1:
    range_cond->addComparison(
        pcl::FieldComparison<pcl::PointXYZ>::ConstPtr(
            new pcl::FieldComparison<pcl::PointXYZ>("z", pcl::ComparisonOps::LT, Z_DISTANCE)));
    
    // 创建条件滤波器并执行
    pcl::ConditionalRemoval<pcl::PointXYZ> condrem;   // 1. 创建滤波器
    condrem.setCondition(range_cond);                 // 2. 告诉它用什么规则
    condrem.setInputCloud(raw_cloud);                 // 3. 告诉它处理哪个点云
    condrem.setKeepOrganized(true);                   // 4. 设置"保留结构"
    condrem.filter(*cloud_filte);                     // 5. 执行，结果存到 cloud_filte
   
    //  体素滤波：进行降采样
    pcl::ApproximateVoxelGrid<pcl::PointXYZ> voxel;
    voxel.setInputCloud(cloud_filte);
    voxel.setLeafSize(0.1f, 0.1f, 0.1f); // 10cm 体素
    voxel.filter(*cloud_filte);
    
    //欧式聚类
    std::vector<Eigen::Vector3f> pointCloudVector;
    if(!cloud_filte->empty())
    {   //创建kd树
        pcl::search::KdTree<pcl::PointXYZ>::Ptr tree(new pcl::search::KdTree<pcl::PointXYZ>());
        tree->setInputCloud(cloud_filte);

        //配置欧氏聚类提取器
        std::vector<pcl::PointIndices> cluster_indices;
        pcl::EuclideanClusterExtraction<pcl::PointXYZ> ec;
        ec.setClusterTolerance(ClusterTolerance); //聚类半径
        ec.setMinClusterSize(MinClusterSize);     // 最小聚类大小，小于则丢弃(噪点)
        ec.setMaxClusterSize(MaxClusterSize);     // 最大聚类大小，超过则丢弃
        ec.setSearchMethod(tree);
        ec.setInputCloud(cloud_filte);
        ec.extract(cluster_indices);

        //遍历每一个簇，找出距离雷达最近的代表点
         static constexpr int CLUSTER_POINTS_TO_KEEP = 4;  // 可调参数

        for (const auto& cluster : cluster_indices) 
        {
            // 1. 收集当前簇中所有点的距离和坐标
            std::vector<std::pair<float, Eigen::Vector3f>> points_with_dist;
            points_with_dist.reserve(cluster.indices.size()); //提前分配空间，避免自动扩容

            for (int index : cluster.indices) 
            {
                Eigen::Vector3f point(cloud_filte->points[index].x, 
                                      cloud_filte->points[index].y, 
                                      cloud_filte->points[index].z);
                float distance = point.norm();  // 到船的距离（2D 欧氏距离）
                points_with_dist.emplace_back(distance, point);
            }

            // 2. 按距离从小到大排序    （Lambda 结构）
            std::sort(points_with_dist.begin(), points_with_dist.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });

            // 3. 取前 N 个最近点（保留宽度信息）
            int keep = std::min(CLUSTER_POINTS_TO_KEEP, (int)points_with_dist.size());
            for (int i = 0; i < keep; ++i) 
            {
                pointCloudVector.emplace_back(points_with_dist[i].second);
            }
        }
    }
    MDATA_SHIP_LidarData_t LidarData;
    int final_num_points = pointCloudVector.size();
    
    LidarData.nums = (final_num_points > 100) ? 100 : static_cast<uint8_t>(final_num_points);
    
    for (int i = 0; i < LidarData.nums; ++i) 
    {
        LidarData.x[i] = pointCloudVector[i].x();
        LidarData.y[i] = pointCloudVector[i].y();
    }

    // 存入抽屉！
    driver->shared_state_.WritePointCloud(LidarData);

    std::cout << "100ms 攒帧完成! 原始点数: " << raw_cloud->points.size() 
              << " | 滤波后点数: " << cloud_filte->points.size() << std::endl;
    
            // 5.更新时间戳，清空大碗
    last_time = current_time;
    point_cloud_buffer.clear();
}

