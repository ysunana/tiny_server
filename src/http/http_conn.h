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
#include <sys/uio.h>
#include <sys/sendfile.h>
#include "src/mysql/sql_connection_pool.h"
#include "src/log/log.h"
#include "src/utils/jwt_util.h"
#include "src/utils/config.h"

class http_conn {
public:
    static const int READ_BUFFER_SIZE = 2048; // 读缓冲区大小
    static const int WRITE_BUFFER_SIZE = 1024;
    
    // 状态机枚举：正在解析请求行，还是正在解析头部
    enum CHECK_STATE { REQUESTLINE, HEADER }; 
    
    http_conn();
    ~http_conn() {}

    void init(int sockfd); 
    void init();
    void process(); 
    bool is_keep_alive() const { return m_is_keep_alive; }
    bool write();                  // 真正的发货状态机
    bool read();
    int get_bytes_to_send() const { return bytes_to_send; } // 查看还有多少没发完

private:
    int m_sockfd;
    char m_read_buf[READ_BUFFER_SIZE]; 
    int m_read_idx;
    char m_write_buf[WRITE_BUFFER_SIZE]; // 专门存放 HTTP 响应头
    int m_write_idx;                     // 响应头的长度

    CHECK_STATE m_check_state;         
    char* m_url;  // 用来临时保存解析出的路径，供后面发文件用
    char* m_query_string;

    bool m_is_post;         // 是不是 POST 请求？
    int m_content_length;   // 请求体有多长？
    char* m_post_data;      // 存放账号密码的字符串首地址
    char* m_jwt_token;      // 存放客人递过来的防伪手环
    bool m_is_keep_alive;
    
    int bytes_to_send;                   // 这一单总共要发多少字节
    int bytes_have_send;                 // 目前已经发了多少字节

    int m_file_fd;
    size_t m_file_size;                  // 映射文件的大小

    void parse_request_line(char* text);
    void parse_headers(char* text);
    void do_request(const char* url); 

    bool verify_login(const char* name, const char* pwd);
};

#endif