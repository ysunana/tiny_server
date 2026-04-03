#include "http_conn.h"
#include <iostream>

http_conn::http_conn() {
    m_sockfd = -1; // -1 代表这个长工目前闲置，没有接待客人
    m_url = nullptr;
    m_post_data = nullptr;
    m_file_address = nullptr;
    // 其他指针也置空即可，不需要在这里 memset，因为没人用
}

void http_conn::init(int fd) {
    m_sockfd = fd;
    m_check_state = REQUESTLINE;
    m_is_post = false;
    m_content_length = 0;
    m_is_keep_alive = false;
    
    m_read_idx = 0;
    m_url = nullptr;
    m_post_data = nullptr;
    m_jwt_token = nullptr;
    
    // 重置发货记账本
    m_write_idx = 0;
    bytes_to_send = 0;
    bytes_have_send = 0;
    unmap(); // 清理上一任客人可能的残留
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
}

// 释放内存的专属函数
void http_conn::unmap() {
    if (m_file_address) {
        munmap(m_file_address, m_file_size);
        m_file_address = nullptr;
    }
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
    // ==========================================
    // 第一刀：彻底抽干内核缓冲区 (ET 模式铁律)
    // ==========================================
    
    while (true) {
        // 从上一次读的末尾接着往里写，防止覆盖
        int bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, MSG_DONTWAIT);
        
        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // 核心：内核数据已被彻底抽干，可以安心去解析了！
            }
            return; // 发生了真正的致命错误，直接退出
        } else if (bytes_read == 0) {
            return; // 客户端主动关掉了连接
        }
        
        m_read_idx += bytes_read;
        
        // 如果缓冲区塞满了，也必须停下，防止内存溢出
        if (m_read_idx >= READ_BUFFER_SIZE) {
            break; 
        }
    }

    if (m_read_idx == 0) return; // 什么都没读到，直接溜

    // ==========================================
    // 第二刀：循环处理缓冲区里的所有请求 (防 Pipeline 陷阱)
    // ==========================================
    int start_index = 0;

    while (true) {
        char* line = get_line_from_buf(m_read_buf, start_index);
        
        if (!line) break; // 缓冲区里的完整行都解析完了，跳出循环，等下一次 Epoll 通知

        if (m_check_state == REQUESTLINE) {
            parse_request_line(line);
            m_check_state = HEADER; 

        } else if (m_check_state == HEADER) {
            if (strlen(line) == 0) {
                if (m_content_length != 0) {
                    m_post_data = m_read_buf + start_index; 
                    *(m_post_data + m_content_length) = '\0';
                }

                do_request(m_url);  // 处理当前这个请求，发文件！

                // 绝对不能在这里 return！
                // 因为 m_read_buf 后面可能还跟着 wrk 发来的下一个请求！
                // 我们必须重置状态，继续下一轮 while 循环剥洋葱！
                m_check_state = REQUESTLINE; 
                m_content_length = 0;
                m_url = nullptr; // 只需要把它置空就行了！
                m_is_post = false;
                
                // 如果是 POST，还需要把 start_index 往后跳过 Body 的长度，以免下一轮解析出错
                if (m_post_data != nullptr) {
                    start_index += m_content_length;
                    m_post_data = nullptr;
                }
                
                // 继续 while 循环，看看还有没有下一个请求...
            } else {
                parse_headers(line);
            }
        }
    }

    // 如果 start_index 小于 m_read_idx，说明缓冲区最后面还有没剥完的“半截洋葱”（TCP 粘包造成的下一个请求的头部）
    if (start_index < m_read_idx) {
        int leftover_len = m_read_idx - start_index;
        // 把这半截有用的数据，整体平移到缓冲区的最头部！
        // (注意：必须用 memmove，不能用 memcpy，因为内存可能会重叠)
        memmove(m_read_buf, m_read_buf + start_index, leftover_len);
        m_read_idx = leftover_len;
    } else {
        // 洋葱全部剥完了，没有任何残留，直接清零书签！
        m_read_idx = 0;
    }
    
    // 把缓冲区后面的脏数据全部抹除，以绝后患
    memset(m_read_buf + m_read_idx, '\0', READ_BUFFER_SIZE - m_read_idx);
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
    if (strncasecmp(text, "Content-Length:", 15) == 0) {
        text += 15;
        // 跳过空格
        text += strspn(text, " \t");
        m_content_length = atol(text); // 字符串转数字
    }

    else if (strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        text += strspn(text, " \t"); // 跳过冒号后面的空格
        
        if (strncasecmp(text, "keep-alive", 10) == 0) {
            m_is_keep_alive = true;
        }
    }

    // 查验客人有没有戴防伪手环
    else if (strncasecmp(text, "Authorization:", 14) == 0) {
        text += 14;
        text += strspn(text, " \t"); // 跳过空格
        if (strncasecmp(text, "Bearer ", 7) == 0) {
            m_jwt_token = text + 7; // 拿到真正的 Token 字符串！
        }
    }
}

void http_conn::do_request(const char* url) {
    bytes_have_send = 0;

    // 【拦截：动态登录接口】
    if (m_is_post && strcmp(url, "/login") == 0) {
        // LOG_INFO("[API 请求] 尝试登录，接收到数据: %s ", (m_post_data ? m_post_data : "无"));
        
        // 简单暴力地解析 "user=xxx&password=yyy" (实际项目会用正则或专门的分割函数)
        char name[50] = {0}, pwd[50] = {0};
        // sscanf 是 C 语言神器，按照格式提取字符串
        if (m_post_data) {
            sscanf(m_post_data, "user=%[^&]&password=%s", name, pwd);
            printf("[Debug] 收到原始数据: %s | 解析出账号: '%s', 密码: '%s'\n", m_post_data, name, pwd);
        }

        // 调用刚才写的 MySQL 校验函数！
        bool login_success = verify_login(name, pwd);

        // 返回 JSON 给前端
        char response_body[1024];
        if (login_success) {
            // 召唤 OpenSSL，生成绝对防伪的 JWT 手环
            std::string token = JWTUtil::Generate(name);
            
            // 把手环塞进 JSON 里发给客户端
            sprintf(response_body, 
                    "{\"status\": \"success\", \"message\": \"欢迎回来, %s大师!\", \"token\": \"%s\"}", 
                    name, token.c_str());
        } else {
            sprintf(response_body, "{\"status\": \"error\", \"message\": \"账号或密码错误!\"}");
        }

       // 把响应头写入专门的 write_buf
        m_write_idx = sprintf(m_write_buf, 
                "HTTP/1.1 200 OK\r\n"
                "Connection: %s\r\n"
                "Content-Type: application/json; charset=utf-8\r\n"
                "Content-Length: %zu\r\n\r\n",
                (m_is_keep_alive ? "keep-alive" : "close"), 
                strlen(response_body));

        // 身体紧跟在头部后面
        strcpy(m_write_buf + m_write_idx, response_body);
        m_write_idx += strlen(response_body);

        // 记账：这单只有 1 块内存（全在 m_write_buf 里）
        m_iv[0].iov_base = m_write_buf;
        m_iv[0].iov_len = m_write_idx;
        m_iv_count = 1;
        bytes_to_send = m_write_idx;
        return; 
    }


    // ==========================================
    // 【拦截：受 JWT 保护的内部 API 路由】
    // ==========================================
    if (strcmp(url, "/api/hello") == 0) {
        std::string current_user = "Stranger";
        char response_body[512];
        int status_code = 200;
        const char* status_text = "OK";

        // 看看客人带没带手环？
        if (m_jwt_token == nullptr) {
            status_code = 401;
            status_text = "Unauthorized";
            sprintf(response_body, "{\"status\": \"error\", \"message\": \"Access Denied: 没戴 VIP 手环，给我出去！\"}");
        } 
        // 带了手环？拿防伪印章验一验！
        else if (!JWTUtil::Verify(m_jwt_token, current_user)) {
            status_code = 401;
            status_text = "Unauthorized";
            sprintf(response_body, "{\"status\": \"error\", \"message\": \"Access Denied: 手环是假的，或者是前朝的剑，抓起来！\"}");
        } 
        // 验票完美通过！
        else {
            // 此时 current_user 已经安全地被 OpenSSL 从手环里提取出来了！
            sprintf(response_body, "{\"status\": \"success\", \"message\": \"尊贵的 %s, 欢迎进入机密系统!\"}", current_user.c_str());
        }

        // 动态拼装响应头（注意状态码是动态的）
        m_write_idx = sprintf(m_write_buf, 
                "HTTP/1.1 %d %s\r\n"
                "Connection: %s\r\n"
                "Content-Type: application/json; charset=utf-8\r\n"
                "Content-Length: %zu\r\n\r\n", 
                status_code, status_text,
                (m_is_keep_alive ? "keep-alive" : "close"), 
                strlen(response_body));

        strcpy(m_write_buf + m_write_idx, response_body);
        m_write_idx += strlen(response_body);

        m_iv[0].iov_base = m_write_buf;
        m_iv[0].iov_len = m_write_idx;
        m_iv_count = 1;
        bytes_to_send = m_write_idx;
        
        return; 
    }
    char path[256];
    sprintf(path, "./resources%s", url); 

    struct stat file_stat;
    
    // 如果文件不存在 (404)
    if (stat(path, &file_stat) < 0) {
        // LOG_ERROR("[404 Not Found] 找不到文件: %s ", path);
        
        // 我们手写一段简陋的 HTML 作为 404 错误页面的正文
        const char* error_body = 
            "<html><head><meta charset=\"utf-8\"><title>404</title></head>"
            "<body><h1 style='color:red;'>404 Not Found</h1>"
            "<p>对不起，您访问的页面被外星人劫持了！</p></body></html>";

        m_write_idx = sprintf(m_write_buf, 
                "HTTP/1.1 404 Not Found\r\n"
                "Connection: %s\r\n"
                "Content-Type: text/html; charset=utf-8\r\n"
                "Content-Length: %zu\r\n\r\n", 
                (m_is_keep_alive ? "keep-alive" : "close"),
                strlen(error_body));

        strcpy(m_write_buf + m_write_idx, error_body);
        m_write_idx += strlen(error_body);

        m_iv[0].iov_base = m_write_buf;
        m_iv[0].iov_len = m_write_idx;
        m_iv_count = 1;
        bytes_to_send = m_write_idx;
        return;

    // 如果文件存在，使用 mmap 零拷贝
    const char* file_type = get_content_type(path);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return;

    m_file_size = file_stat.st_size;
    m_file_address = (char*)mmap(0, m_file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd); 

    // 如果内存映射失败，绝对不能发！
    if (m_file_address == (void*)-1) {
        m_file_address = nullptr;
        return;
    }

   // 头部写入 m_write_buf
    m_write_idx = sprintf(m_write_buf, 
            "HTTP/1.1 200 OK\r\n"
            "Connection: %s\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %ld\r\n\r\n", 
            (m_is_keep_alive ? "keep-alive" : "close"), 
            file_type, m_file_size);
            
    // 记账：这单有 2 块内存（一块头，一块 mmap）
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    
    m_iv[1].iov_base = m_file_address;
    m_iv[1].iov_len = m_file_size;
    
    m_iv_count = 2;
    bytes_to_send = m_write_idx + m_file_size;
    }
}

// 支持断点续传的 write 状态机
bool http_conn::write() {
    int temp = 0;

    // 如果本来就没东西发，任务完成！
    if (bytes_to_send == 0) {
        return true; 
    }

    // 只要网卡没满，就死循环疯狂发！
    while (1) {
        // writev：同时把 m_iv 里的多块内存按顺序发出去
        temp = writev(m_sockfd, m_iv, m_iv_count);
        
        if (temp <= -1) {
            // 网卡喊停了！
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 保留一切现状，返回 true (告诉主线程不要关连接，等下次唤醒)
                return true; 
            }
            // 发生真正的错误 (比如对面拔网线了)，安全释放内存并告辞
            unmap(); 
            return false; 
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;

        // 最烧脑的指针平移：计算下一次该从哪里开始发
        if (bytes_have_send >= m_write_idx) {
            // 情况 A：头部已经全发完了！
            m_iv[0].iov_len = 0; 
            if (m_iv_count == 2) { // 如果有文件身体，平移身体的指针
                m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
                m_iv[1].iov_len = bytes_to_send;
            }
        } else {
            // 情况 B：头部还没发完
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_write_idx - bytes_have_send;
        }

        // 全部发完了！
        if (bytes_to_send <= 0) {
            unmap(); // 卸载货车 (释放 mmap)
            // 如果是短连接，返回 false 让主线程拔网线；长连接返回 true 继续听命
            return m_is_keep_alive; 
        }
    }
}

bool http_conn::verify_login(const char* name, const char* pwd) {
    if (!name || !pwd) return false;

    // ==========================================
    // 第一道防线：极速询问 Redis (内存查找)
    // ==========================================
    // 1. 连接 Redis (注意地址也是 host.docker.internal 跨界网关)
    thread_local redisContext* redis_conn = nullptr;

    // 只有当第一次运行，或者连接意外断开时，才去建连
    if (redis_conn == nullptr || redis_conn->err) {
        if (redis_conn) {
            redisFree(redis_conn); // 清理死掉的旧连接
            redis_conn = nullptr;
        }
        redis_conn = redisConnect("host.docker.internal", 6379);
    }
    
    if (redis_conn != NULL && !redis_conn->err) {
        // 2. 去便利贴上找这个账号的密码
        redisReply *reply = (redisReply *)redisCommand(redis_conn, "GET %s", name);
        
        if (reply != NULL && reply->type == REDIS_REPLY_STRING) {
            // 🎯 缓存命中 (Cache Hit)！
            // 根本不需要去烦 MySQL，直接在内存里比对密码！
            bool is_match = (strcmp(reply->str, pwd) == 0);
            
            // LOG_INFO("[Redis] 命中缓存！账号: %s", name); // 压测时记得保持注释
            
            freeReplyObject(reply);
            return is_match; // 光速返回！
        }
        if (reply) freeReplyObject(reply);
    }

    // ==========================================
    // 第二道防线：Redis 没找到，只能去查 MySQL (磁盘查找)
    // ==========================================
    // LOG_INFO("[Redis] 未命中，去 MySQL 查档案...");

    MYSQL* mysql = nullptr;
    // RAII 机制：自动从单例池中获取连接，函数结束时自动归还！
    SqlConnRAII mysqlcon(&mysql, SqlConnPool::Instance());
    assert(mysql);

    bool flag = false;
    char sql_query[256];
    // 组装 SQL 查询语句
    sprintf(sql_query, "SELECT passwd FROM user WHERE username='%s' LIMIT 1", name);

    // 执行 SQL 语句
    if (mysql_query(mysql, sql_query) == 0) { 
        MYSQL_RES* res = mysql_store_result(mysql);
        if (res) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row != nullptr) {
                // row[0] 就是查出来的密码
                if (strcmp(row[0], pwd) == 0) {
                    flag = true; // 密码完全匹配！登录成功！

                    if (redis_conn != NULL && !redis_conn->err) {
                        // SET 账号 密码
                        redisReply *set_reply = (redisReply *)redisCommand(redis_conn, "SET %s %s", name, row[0]);
                        if (set_reply) freeReplyObject(set_reply);
                        // LOG_INFO("[Redis] 档案已同步至内存便利贴！");
                    }

                } else {
                    // LOG_ERROR("[DB] 密码错误！");
                }
            } else {
                // LOG_ERROR("[DB] 用户不存在！");
            }

            // 释放结果集内存 (重要，防内存泄露)
            mysql_free_result(res); 
        }
    }

    return flag;    
}