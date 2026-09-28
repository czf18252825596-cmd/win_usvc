#include "config/Config.h"
#include <fstream>

namespace {
//去掉两端空格
std::string Trim(const std::string& s) {
    const char* ws = " \t";
    auto a = s.find_first_not_of(ws);
    if (a == std::string::npos) return {};
    auto b = s.find_last_not_of(ws);
    return s.substr(a, b - a + 1);
}

}  // namespace

bool Config::Load(const std::string& path) {
    std::ifstream in(path); //读文件
    if (!in.is_open()) return false;

    std::string section;
    std::string line;
    while (std::getline(in, line)) {//读一行
        line = Trim(line);
        if (line.empty()) continue;                    // 空行  跳过
        if (line[0] == ';' || line[0] == '#') continue; // 注释 跳过

        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2); // [navigation] -> navigation
            continue;
        }

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;         // 不是 key=value 跳过
        std::string key = Trim(line.substr(0, eq));
        std::string val = Trim(line.substr(eq + 1));
        data_[section][key] = val;
    }
    return true;
}