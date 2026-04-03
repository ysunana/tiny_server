#ifndef JWT_UTIL_H
#define JWT_UTIL_H

#include <string>
#include <vector>

class JWTUtil {
public:
    // 我们的绝密印章 (实际开发中不能写死在代码里，通常从环境变量读)
    static const std::string SECRET_KEY;

    // 1. 给客人颁发手环
    static std::string Generate(const std::string& username);

    // 2. 检验客人的手环，如果通过，把名字提取到 out_username 里
    static bool Verify(const std::string& token, std::string& out_username);

private:
    // 底层黑科技：Base64Url 编码与 HMAC-SHA256 哈希加密
    static std::string Base64UrlEncode(const unsigned char* buffer, size_t length);
    static std::string Base64UrlEncode(const std::string& data);
    static std::string Base64UrlDecode(const std::string& input);
    static std::string HmacSha256(const std::string& data, const std::string& key);
};

#endif