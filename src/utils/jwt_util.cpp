#include "jwt_util.h"
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <iostream>

// 绝密印章，谁也别想伪造！
const std::string JWTUtil::SECRET_KEY = "Anhui_Engineering_University_Super_Secret_Key_2026";

// 静态常量：JWT 的头部 (写死了我们用的算法是 HS256)
const std::string JWT_HEADER = "{\"alg\":\"HS256\",\"typ\":\"JWT\"}";

std::string JWTUtil::Generate(const std::string& username) {
    // 1. 组装 Payload (正文)
    std::string payload = "{\"user\":\"" + username + "\"}";

    // 2. 将 Header 和 Payload 进行 Base64Url 编码
    std::string encoded_header = Base64UrlEncode(JWT_HEADER);
    std::string encoded_payload = Base64UrlEncode(payload);

    // 3. 拼接准备签名的数据
    std::string sign_data = encoded_header + "." + encoded_payload;

    // 4. 使用 OpenSSL 进行 HMAC-SHA256 签名，并再次 Base64Url 编码
    std::string signature = HmacSha256(sign_data, SECRET_KEY);

    // 5. 生成最终的防伪手环！
    return sign_data + "." + signature;
}

bool JWTUtil::Verify(const std::string& token, std::string& out_username) {
    // 1. 找找手环里有没有两个点 "." (JWT 标准格式)
    size_t first_dot = token.find('.');
    size_t second_dot = token.rfind('.');
    
    if (first_dot == std::string::npos || second_dot == std::string::npos || first_dot == second_dot) {
        return false; // 格式不对，直接当作假票！
    }

    // 2. 把手环拆成三截
    std::string encoded_header = token.substr(0, first_dot);
    std::string encoded_payload = token.substr(first_dot + 1, second_dot - first_dot - 1);
    std::string provided_signature = token.substr(second_dot + 1);

    // 3. 拿出我们的印章，重新算一遍签名
    std::string sign_data = encoded_header + "." + encoded_payload;
    std::string expected_signature = HmacSha256(sign_data, SECRET_KEY);

    // 4. 比对签名！如果对不上，说明有人篡改了内容！
    if (provided_signature != expected_signature) {
        return false; 
    }

    // 5. 签名验证通过！解码 Payload 提取用户名
    std::string payload_json = Base64UrlDecode(encoded_payload);
    
    // 极其简陋的 JSON 提取 (寻找 "user":"xxx")
    size_t pos = payload_json.find("\"user\":\"");
    if (pos != std::string::npos) {
        pos += 8; // 跳过 "user":"
        size_t end_pos = payload_json.find("\"", pos);
        if (end_pos != std::string::npos) {
            out_username = payload_json.substr(pos, end_pos - pos);
            return true; // 验票完美通过！
        }
    }
    return false;
}

// ==========================================
// 下面是底层 Base64Url 和 SHA256 加密算法实现 (纯 C++ 苦力活)
// ==========================================

std::string JWTUtil::HmacSha256(const std::string& data, const std::string& key) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    
    // 直接调用 OpenSSL 汇编级的高速加密函数！
    HMAC(EVP_sha256(), key.c_str(), key.length(), 
         (const unsigned char*)data.c_str(), data.length(), hash, &length);
         
    return Base64UrlEncode(hash, length);
}

static const std::string base64_chars = 
             "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
             "abcdefghijklmnopqrstuvwxyz"
             "0123456789+/";

std::string JWTUtil::Base64UrlEncode(const unsigned char* bytes_to_encode, size_t in_len) {
    std::string ret;
    int i = 0, j = 0;
    unsigned char char_array_3[3], char_array_4[4];

    while (in_len--) {
        char_array_3[i++] = *(bytes_to_encode++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            for(i = 0; (i <4) ; i++) ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }
    if (i) {
        for(j = i; j < 3; j++) char_array_3[j] = '\0';
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        for (j = 0; (j < i + 1); j++) ret += base64_chars[char_array_4[j]];
    }

    // 将普通 Base64 转为 URL 安全模式 (替换 + 和 /，去掉 =)
    for (char& c : ret) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    return ret;
}

std::string JWTUtil::Base64UrlEncode(const std::string& data) {
    return Base64UrlEncode((const unsigned char*)data.c_str(), data.length());
}

std::string JWTUtil::Base64UrlDecode(const std::string& input) {
    std::string encoded = input;
    // 恢复 Base64 格式
    for (char& c : encoded) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    // 补齐 = 
    while (encoded.length() % 4 != 0) encoded += "=";

    int in_len = encoded.length();
    int i = 0, j = 0, in_ = 0;
    unsigned char char_array_4[4], char_array_3[3];
    std::string ret;

    while (in_len-- && ( encoded[in_] != '=') && (isalnum(encoded[in_]) || (encoded[in_] == '+') || (encoded[in_] == '/'))) {
        char_array_4[i++] = encoded[in_]; in_++;
        if (i == 4) {
            for (i = 0; i <4; i++) char_array_4[i] = base64_chars.find(char_array_4[i]);
            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
            for (i = 0; (i < 3); i++) ret += char_array_3[i];
            i = 0;
        }
    }
    if (i) {
        for (j = 0; j < i; j++) char_array_4[j] = base64_chars.find(char_array_4[j]);
        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        for (j = 0; (j < i - 1); j++) ret += char_array_3[j];
    }
    return ret;
}