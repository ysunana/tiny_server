#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <unordered_map>

class Config {
public:
    // 单例模式，全局唯一
    static Config* Instance() {
        static Config config;
        return &config;
    }

    // 加载配置文件
    bool Load(const char* file_name);

    // 获取字符串配置
    std::string GetString(const std::string& key, const std::string& default_val = "");
    
    // 获取整数配置
    int GetInt(const std::string& key, int default_val = 0);

private:
    Config() = default;
    ~Config() = default;
    
    // 核心数据结构：哈希表，实现 O(1) 极速查找
    std::unordered_map<std::string, std::string> config_map_;
};

#endif // CONFIG_H