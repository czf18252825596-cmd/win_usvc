#pragma once//只编译一次
#include <shared_mutex>
#include <queue>      // 队列库
#include <vector>     // 动态数组库

#include "NavData.h"
extern "C" {
#include "MTLink_DataStruct.h" 
}

// --- 定义一个结构体，用来打包每一条透传指令 ---
struct RawDataPacket {
    uint16_t obj;                 // 指令的 ID (如 OBJ_SHIP_BoardReset)
    std::vector<uint8_t> payload; // 动态长度的负载数据
};

// 所有线程共享的数据抽屉。
// 每个数据块 = 字段 + 锁 + ReadXxx()/WriteXxx() 四件套，捆在一起保证线程安全。
// 新增模块（船/上位机/雷达）：按同样四件套往这个类里追加一组建数据即可。
class SharedState {
public:
    //导航数据
    // 读快照：加读锁 → 拷贝一份返回 → 锁立刻释放（临界区极短）
    NavData ReadNav() const {
        std::shared_lock<std::shared_mutex> lk(mtx_);
        return nav_;
    }
    // 写快照：加写锁 → 覆盖 → 锁立刻释放
    void WriteNav(const NavData& nav) {
        std::unique_lock<std::shared_mutex> lk(mtx_);
        nav_ = nav;
    }

    //船ID
    MDATA_SHIP_ID_t ReadID() const {
        std::shared_lock<std::shared_mutex> lk(mtx_);
        return ship_id_;
    }
    void WriteShipID(const MDATA_SHIP_ID_t& ship_id) {
        std::unique_lock<std::shared_mutex> lk(mtx_);    
        ship_id_ = ship_id;
    }

    //船控制RunCtrl
    MDATA_SHIP_RunCtrl_t ReadRunCtrl() const {
        std::shared_lock<std::shared_mutex> lk(mtx_);
        return run_ctrl_;
    }
    void WriteRunCtrl(const MDATA_SHIP_RunCtrl_t& run_ctrl){
        std::unique_lock<std::shared_mutex> lk(mtx_);
        run_ctrl_ = run_ctrl;
    }

    // 船体状态反馈 StateData
    MDATA_SHIP_StateData_t ReadShipState() const {
        std::shared_lock<std::shared_mutex> lk(mtx_);
        return ship_state_;
    }
    void WriteShipState(const MDATA_SHIP_StateData_t& state) {
        std::unique_lock<std::shared_mutex> lk(mtx_);
        ship_state_ = state;
    }

    MDATA_SHIP_ACHData_t ReadACHData() const{
        std::shared_lock<std::shared_mutex> lk(mtx_);
        return achdata_;
    }
    void WriteACHData(const MDATA_SHIP_ACHData_t& achdata){
        std::unique_lock<std::shared_mutex> lk(mtx_);
        achdata_ = achdata;
    }


        //Lidar读取
    MDATA_SHIP_LidarData_t ReadPointCloud() const {
        std::shared_lock<std::shared_mutex> lk(mtx_);
        return lidar_data_; 
    }
    void WritePointCloud(MDATA_SHIP_LidarData_t cloud) {
        std::unique_lock<std::shared_mutex> lk(mtx_);
        lidar_data_ = cloud;
    }

        // 供 (upper_node) 调用：把指令塞进队列尾部
    void PushRawDataUp(uint16_t obj, const uint8_t* buf, uint16_t len) {
        std::unique_lock<std::shared_mutex> lk(mtx_); // 修改队列结构，必须用写锁 (unique_lock)
        // 如果积压超过 50 条，丢弃最老的指令
        if (raw_data_up_queue_.size() >= 50) {
            raw_data_up_queue_.pop(); 
        }
        RawDataPacket packet;
        packet.obj = obj;
        packet.payload.assign(buf, buf + len); // 将 C 数组拷贝进 vector
        
        raw_data_up_queue_.push(packet);
    }
    void PushRawDataDown(uint16_t obj, const uint8_t* buf, uint16_t len) {
        std::unique_lock<std::shared_mutex> lk(mtx_); 

        if (raw_data_down_queue_.size() >= 50) {
            raw_data_down_queue_.pop(); 
        }
        RawDataPacket packet;
        packet.obj = obj;
        packet.payload.assign(buf, buf + len); 
        raw_data_down_queue_.push(packet);
    }

    // 消费者 (ship_node) 调用：尝试从队列头部拿一条指令
    // 返回值 bool：表示拿到了没有（队列可能为空）
    bool PopRawDataUp(RawDataPacket& out_packet) {
        std::unique_lock<std::shared_mutex> lk(mtx_); // 弹出数据会修改队列，所以读队列这里也必须用写锁！
        
        if (raw_data_up_queue_.empty()) {
            return false; // 队列是空的，没指令
        }
        
        out_packet = raw_data_up_queue_.front(); // 拿出最前面的一条
        raw_data_up_queue_.pop();                // 从队列里删掉它
        return true;
    }
    bool PopRawDataDown(RawDataPacket& out_packet) {
        std::unique_lock<std::shared_mutex> lk(mtx_); 
        if (raw_data_down_queue_.empty()) return false;
        out_packet = raw_data_down_queue_.front(); 
        raw_data_down_queue_.pop();                
        return true;
    }
    //新任务文件
    bool ReadNewTaskFlag() const{
        std::shared_lock<std::shared_mutex> lk(mtx_);
        return has_new_task_;
    }
    void WriteNewTaskFlag(bool flag){
        std::unique_lock<std::shared_mutex> lk(mtx_);
        has_new_task_ = flag;
    }


private:
    mutable std::shared_mutex mtx_;   // 读多写少，用共享锁降低竞争
    NavData nav_;

    MDATA_SHIP_ID_t         ship_id_   = {0};
    MDATA_SHIP_RunCtrl_t    run_ctrl_  = {0};
    MDATA_SHIP_StateData_t ship_state_ = {0};
    bool has_new_task_ = false;

    MDATA_SHIP_ACHData_t    achdata_   = {0};
    MDATA_SHIP_LidarData_t lidar_data_ = {0};

    //用于存放透传指令的队列
    std::queue<RawDataPacket> raw_data_down_queue_; // 用于下发
    std::queue<RawDataPacket> raw_data_up_queue_;   // 用于上传
};