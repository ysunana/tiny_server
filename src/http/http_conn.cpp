#include "http_conn.h"
#include <iostream>

// 构造函数：初始化变量
http_conn::http_conn(int fd) : m_sockfd(fd), m_check_state(REQUESTLINE), m_url(nullptr), 
                               m_is_post(false), m_content_length(0), m_post_data(nullptr) {
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
}

// 切割一行的工具函数
char* get_line_from_buf(char* buffer, int& start_index) {
    char* line = buffer + start_index;
    for (int i = start_index; buffer[i] != '\0'; ++i) {
        if (buffer[i] == '\r' && buffer[i+1] == '\n') {
            buffer[i] = '\0';
            buffer[i+1] = '\0';
            start_index = i + 2;
            return line;
        }
    }
    return nullptr;
}

const char* get_content_type(const char* name) {
    // strrchr: 这是一个 C 语言神级函数，从右向左查找字符。
    // 这里我们用它来找文件名里最后一个 '.' 的位置
    const char* dot = strrchr(name, '.'); 
    
    // 如果文件名里没有点（比如访问了一个无后缀的路径），默认当纯文本处理
    if (!dot) return "text/plain"; 

    // strcmp: 字符串比较。注意，C语言不能用 == 比较字符串内容！
    if (strcmp(dot, ".html") == 0) return "text/html";
    if (strcmp(dot, ".css")  == 0) return "text/css";
    if (strcmp(dot, ".js")   == 0) return "application/javascript";
    if (strcmp(dot, ".jpg")  == 0) return "image/jpeg";
    if (strcmp(dot, ".png")  == 0) return "image/png";
    if (strcmp(dot, ".ico")  == 0) return "image/x-icon";
    
    // 如果都不认识，也作为纯文本兜底
    return "text/plain"; 
}

void http_conn::process() {
    // 1. 把浏览器发来的数据读进缓冲区
    int bytes_read = recv(m_sockfd, m_read_buf, READ_BUFFER_SIZE, 0);
    if (bytes_read <= 0) return;

    int start_index = 0;

    // 2. 状态机正式开始“剥洋葱”
    while (true) {
        char* line = get_line_from_buf(m_read_buf, start_index);
        if (!line) break;

        if (m_check_state == REQUESTLINE) {
            // 解析第一行，这里会把路径提取出来存进 m_url
            parse_request_line(line);
            m_check_state = HEADER; // 拨完第一层，状态切换到查 Header

        } else if (m_check_state == HEADER) {
            // 如果遇到长度为 0 的行（空行），说明 Header 读完了
            if (strlen(line) == 0) {
                if (m_content_length != 0) {
                    // 说明是 POST 请求，且带有数据。
                    // 此时 start_index 刚好越过了空行的 \r\n，指向了请求体(Body)的第一块肉！
                    m_post_data = m_read_buf + start_index; 
                    
                    // 为了安全，我们在数据末尾手动加个 '\0' 截断它
                    *(m_post_data + m_content_length) = '\0';
                }

                do_request(m_url);  // 【调用点】拿着第一步存好的 m_url 去拿文件！
                return;             // 文件发完，整个处理流程结束
            }
            // 不是空行，就当作普通 Header 解析（目前先跳过）
            parse_headers(line);
        }
    }
}

void http_conn::parse_request_line(char* text) {
    if (strncasecmp(text, "POST", 4) == 0) {
        m_is_post = true;
    }

    // 从 "GET /index.html HTTP/1.1" 中抠出 "/index.html"
    m_url = strpbrk(text, " \t"); 
    if (m_url) {
        *m_url++ = '\0'; 
        char* version = strpbrk(m_url, " \t"); 
        if (version) *version++ = '\0';

        // 【新增逻辑】处理 GET 请求的带参查询 (Query String)
        // 比如把 "/api/hello?name=Sunana" 劈成两半
        m_query_string = strchr(m_url, '?'); // 找问号
        if (m_query_string) {
            // 如果找到了问号，把问号变成结束符 \0。
            // 这样 m_url 就变成了纯净的路径 "/api/hello"
            *m_query_string++ = '\0'; 
            // 此时 m_query_string 指向 "name=Sunana"
        } else {
            m_query_string = nullptr; // 没有参数
        }
        
        // 如果访问的是根目录，默认给 index.html
        if (strcmp(m_url, "/") == 0) {
            m_url = (char*)"/index.html";
        }
    }
}

void http_conn::parse_headers(char* text) {
    // 【新增】如果是 Content-Length 字段，把长度数字扣出来
    if (strncasecmp(text, "Content-Length:", 15) == 0) {
        text += 15;
        // 跳过空格
        text += strspn(text, " \t");
        m_content_length = atol(text); // 字符串转数字
    }
}

void http_conn::do_request(const char* url) {
    // 【新增】拦截 POST 登录请求
    if (m_is_post && strcmp(url, "/login") == 0) {
        LOG_INFO("[API 请求] 尝试登录，接收到数据: %s ", (m_post_data ? m_post_data : "无"));
        
        // 简单暴力地解析 "user=xxx&password=yyy" (实际项目会用正则或专门的分割函数)
        char name[50] = {0}, pwd[50] = {0};
        // sscanf 是 C 语言神器，按照格式提取字符串
        if (m_post_data) {
            sscanf(m_post_data, "user=%[^&]&password=%s", name, pwd);
        }

        // 调用刚才写的 MySQL 校验函数！
        bool login_success = verify_login(name, pwd);

        // 返回 JSON 给前端
        char response_body[512];
        if (login_success) {
            sprintf(response_body, "{\"status\": \"success\", \"message\": \"欢迎回来, %s大师!\"}", name);
        } else {
            sprintf(response_body, "{\"status\": \"error\", \"message\": \"账号或密码错误!\"}");
        }

        char header[512];
        sprintf(header, 
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json; charset=utf-8\r\n"
                "Content-Length: %zu\r\n\r\n", strlen(response_body));

        send(m_sockfd, header, strlen(header), 0);
        send(m_sockfd, response_body, strlen(response_body), 0);
        return; 
    }


    // 【新增逻辑】API 路由拦截 (Controller)
    // 拦截对 "/api/hello" 的访问，直接返回动态生成的 JSON 数据
    if (strcmp(url, "/api/hello") == 0) {
        LOG_INFO("[API 请求] 访问了 hello 接口，携带参数: %s ", (m_query_string ? m_query_string : "无"));

        // 提取参数内容 (简单的字符串查找，实际项目会用正则或专门的解析库)
        const char* default_name = "Stranger";
        if (m_query_string && strncmp(m_query_string, "name=", 5) == 0) {
            default_name = m_query_string + 5; // 跳过 "name="，拿到后面的值
        }

        // 动态拼装 JSON 正文
        char response_body[512];
        sprintf(response_body, "{\"status\": \"success\", \"message\": \"Hello, %s! Welcome to Anhui Engineering University Server.\"}", default_name);

        // 拼装 HTTP 响应头 (注意 Content-Type 变成了 application/json)
        char header[512];
        sprintf(header, 
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json; charset=utf-8\r\n"
                "Content-Length: %zu\r\n"
                "\r\n", strlen(response_body));

        // 直接发送内存里生成的动态数据，然后 return！不再往下走 mmap 静态文件逻辑
        send(m_sockfd, header, strlen(header), 0);
        send(m_sockfd, response_body, strlen(response_body), 0);
        return; 
    }

    char path[256];
    sprintf(path, "./resources%s", url); 

    struct stat file_stat;
    
    // ---------------------------------------------------------
    // 第一步：查户口。如果 stat 返回 < 0，说明文件在硬盘上不存在
    // ---------------------------------------------------------
    if (stat(path, &file_stat) < 0) {
        LOG_ERROR("[404 Not Found] 找不到文件: %s ", path);
        
        // 我们手写一段简陋的 HTML 作为 404 错误页面的正文
        const char* error_body = 
            "<html><head><meta charset=\"utf-8\"><title>404</title></head>"
            "<body><h1 style='color:red;'>404 Not Found</h1>"
            "<p>对不起，您访问的页面被外星人劫持了！</p></body></html>";

        char header[512];
        // 组装 404 响应头：注意状态码变成了 404 Not Found
        sprintf(header, 
                "HTTP/1.1 404 Not Found\r\n"
                "Content-Type: text/html; charset=utf-8\r\n"
                "Content-Length: %zu\r\n"
                "\r\n", strlen(error_body));

        // 先发头部，再发正文，然后直接 return 结束任务
        send(m_sockfd, header, strlen(header), 0);
        send(m_sockfd, error_body, strlen(error_body), 0);
        return; 
    }

    // ---------------------------------------------------------
    // 第二步：文件存在！获取它的正确类型
    // ---------------------------------------------------------
    const char* file_type = get_content_type(path);

    // ---------------------------------------------------------
    // 第三步：mmap 内存映射 (零拷贝)
    // ---------------------------------------------------------
    int fd = open(path, O_RDONLY);
    char* file_address = (char*)mmap(0, file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd); // 映射完内存，文件描述符就可以关了

    // ---------------------------------------------------------
    // 第四步：组装 200 OK 响应头，并发送
    // ---------------------------------------------------------
    char header[512];
    // 这里用 %s 把刚才获取的 file_type 填进去！
    sprintf(header, 
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %ld\r\n"
            "\r\n", file_type, file_stat.st_size);
            
    send(m_sockfd, header, strlen(header), 0);

    // ---------------------------------------------------------
    // 第五步：发送文件本体，并释放内存
    // ---------------------------------------------------------
    send(m_sockfd, file_address, file_stat.st_size, 0);
    munmap(file_address, file_stat.st_size); // 极其重要：防止内存泄漏
}

bool http_conn::verify_login(const char* name, const char* pwd) {
    if (!name || !pwd) return false;

    MYSQL* mysql = nullptr;
    // RAII 机制：自动从单例池中获取连接，函数结束时自动归还！
    SqlConnRAII mysqlcon(&mysql, SqlConnPool::Instance());
    assert(mysql);

    bool flag = false;
    char sql_query[256];
    // 组装 SQL 查询语句
    sprintf(sql_query, "SELECT passwd FROM user WHERE username='%s' LIMIT 1", name);

    // 执行 SQL 语句
    if (mysql_query(mysql, sql_query)) { 
        LOG_ERROR("[DB] MySQL 查询失败");
        return false; 
    }

    // 获取查询结果
    MYSQL_RES* res = mysql_store_result(mysql);
    if (!res) return false;

    // 解析结果行
    MYSQL_ROW row = mysql_fetch_row(res);
    if (row != nullptr) {
        // row[0] 就是查出来的密码
        if (strcmp(row[0], pwd) == 0) {
            flag = true; // 密码完全匹配！登录成功！
        } else {
            LOG_ERROR("[DB] 密码错误！");
        }
    } else {
        LOG_ERROR("[DB] 用户不存在！");
    }

    // 释放结果集内存 (重要，防内存泄露)
    mysql_free_result(res); 
    return flag;
}