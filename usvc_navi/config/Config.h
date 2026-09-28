#pragma once

#include <string>
#include <map>
#include <sstream>

// INI 解析器（零依赖），仅加载 [section] 下的 key = value
class Config {
public:
    // 返回 false 表示文件打不开
    bool Load(const std::string& path);

    // 读配置；节点或键不存在时返回默认值 def
    template <typename T>  //模板函数
    T Get(const std::string& section,
          const std::string& key,
          T def = T{}) const 
    {   //函数体
        auto it = data_.find(section);
        if (it == data_.end()) return def;
        auto it2 = it->second.find(key);
        if (it2 == it->second.end()) return def;

        std::istringstream ss(it2->second);//字符串转型，转为输入流
        T v{};
        ss >> v;
        return ss.fail() ? def : v;
    }

private:
    // section -> (key -> value)
    std::map<std::string, std::map<std::string, std::string>> data_;
};