#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <cassert>
#include <hiredis/hiredis.h>
#include "src/mysql/sql_connection_pool.h"
#include "src/log/log.h"

class http_conn {
public:
    static const int READ_BUFFER_SIZE = 2048; // 读缓冲区大小
    
    // 状态机枚举：正在解析请求行，还是正在解析头部
    enum CHECK_STATE { REQUESTLINE, HEADER }; 
    
    http_conn();
    ~http_conn() {}

    void init(int sockfd); // 声明我们即将编写的 init 函数
    void process(); // 核心：被线程池调用的主流程
    bool is_keep_alive() const { return m_is_keep_alive; }

private:
    int m_sockfd;
    char m_read_buf[READ_BUFFER_SIZE]; 
    int m_read_idx;
    CHECK_STATE m_check_state;         
    
    char* m_url;  // 【关键补充】用来临时保存解析出的路径，供后面发文件用
    char* m_query_string;

    bool m_is_post;         // 是不是 POST 请求？
    int m_content_length;   // 请求体有多长？
    char* m_post_data;      // 存放账号密码的字符串首地址

    bool m_is_keep_alive;

    // 三个内部打工函数
    void parse_request_line(char* text);
    void parse_headers(char* text);
    void do_request(const char* url); 

    bool verify_login(const char* name, const char* pwd);
};

#endif