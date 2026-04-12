#include "config.h"
#include <fstream>
#include <iostream>

bool Config::Load(const char* file_name) {
    std::ifstream file(file_name);
    if (!file.is_open()) {
        std::cerr << "[Config] 警告: 找不到配置文件 " << file_name << "，将使用默认参数！\n";
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        // 剔除注释和空行
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        // 寻找等号
        size_t pos = line.find('=');
        if (pos != std::string::npos) {
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);
            
            // 存入哈希表
            config_map_[key] = value;
        }
    }
    file.close();
    return true;
}

std::string Config::GetString(const std::string& key, const std::string& default_val) {
    auto it = config_map_.find(key);
    if (it != config_map_.end()) {
        return it->second;
    }
    return default_val;
}

int Config::GetInt(const std::string& key, int default_val) {
    auto it = config_map_.find(key);
    if (it != config_map_.end()) {
        return std::stoi(it->second);
    }
    return default_val;
}