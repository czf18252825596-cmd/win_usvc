#pragma once//只编译一次
#include <shared_mutex>
#include "NavData/NavData.h"

// 所有线程共享的数据抽屉。
// 每个数据块 = 字段 + 锁 + ReadXxx()/WriteXxx() 四件套，捆在一起保证线程安全。
// 新增模块（船/上位机/雷达）：按同样四件套往这个类里追加一组建数据即可。
class SharedState {
public:
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

private:
    mutable std::shared_mutex mtx_;   // 读多写少，用共享锁降低竞争
    NavData nav_;
};