#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/epoll.h> 
#include <signal.h>
#include <unistd.h>
#include <atomic>
#include <thread>
#include <vector>

#include "src/threadpool/threadpool.h"
#include "src/http/http_conn.h"
#include "src/timer/lru_timer.h"
#include "src/mysql/sql_connection_pool.h"
#include "src/log/log.h"
#include "src/utils/config.h"

#define MAX_EVENTS 1024
#define TIMEOUT_MS 15000 // 15秒无操作直接踢出！
#define MAX_FD 65536

int GLOBAL_LOG_LEVEL = 1;

http_conn* users;
ThreadPool* global_pool = nullptr;
std::atomic<bool> server_is_running{true};

const int SUB_REACTOR_NUM = 4;
int sub_epoll_fds[SUB_REACTOR_NUM];

void handle_shutdown_signal(int sig) {
    // 收到 Ctrl+C (SIGINT) 或 docker stop (SIGTERM)
    printf("\n[System] 收到熄火指令 (Signal: %d)，拉下卷帘门，准备优雅停机...\n", sig);
    fflush(stdout);
    
    server_is_running = false;
}

void set_non_blocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
}

void add_fd_to_epoll(int epoll_fd, int fd, bool is_client) {
    epoll_event event;
    event.data.fd = fd;
    if (is_client) {
        // 客人管家：必须加 EPOLLONESHOT 防踩踏！
        event.events = EPOLLIN | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    } else {
        // 前台接待员 (server_fd)：绝不能加 EPOLLONESHOT，他必须永远醒着！
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    }
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
    set_non_blocking(fd);
}

void sub_reactor_loop(int thread_id) {
    int my_epoll_fd = sub_epoll_fds[thread_id];
    epoll_event events[MAX_EVENTS];

    ListTimer local_timer;

    while (server_is_running) {
        int time_ms = local_timer.getNextTick();

        // 子线程只等自己的客人！
        int number = epoll_wait(my_epoll_fd, events, MAX_EVENTS, time_ms);
        
        if (number < 0 && errno == EINTR) continue; 

        local_timer.tick();

        for (int i = 0; i < number; i++) {
            int sockfd = events[i].data.fd;

            //如果已经拔了网线或者报错了就没必要走后续动作了
            if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // 取消定时器 (如果有这行逻辑的话)
                epoll_ctl(my_epoll_fd, EPOLL_CTL_DEL, sockfd, 0);
                close(sockfd);
                continue;
            }

            // 情况 B：客人发请求来了 (EPOLLIN)
            if (events[i].events & EPOLLIN) {
                // 再次调用 add 时，定时器内部会找到它并自动下沉/上浮调整堆的位置
                local_timer.add(sockfd, TIMEOUT_MS, [sockfd, my_epoll_fd]() {
                    LOG_INFO("[无情监工] 客户端: %d 挂机超过15秒，拔网线！", sockfd);
                    epoll_ctl(my_epoll_fd, EPOLL_CTL_DEL, sockfd, 0);
                    close(sockfd);
                });

                if (users[sockfd].read()) {
                    global_pool->addTask([sockfd, my_epoll_fd] {
                        users[sockfd].process(); // 线程池负责解析 HTTP、查数据库
                        
                        epoll_event event;
                        event.data.fd = sockfd;

                        if (users[sockfd].get_bytes_to_send() > 0) {
                            // 解析出了完整响应，可以切换去发货了
                            event.events = EPOLLOUT | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
                        } else {
                            // 解析失败（数据没收全），继续死等剩下的包！
                            event.events = EPOLLIN | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
                        }
                        epoll_ctl(my_epoll_fd, EPOLL_CTL_MOD, sockfd, &event);
                    });
                } else {
                    // 读失败说明对面断开了
                    epoll_ctl(my_epoll_fd, EPOLL_CTL_DEL, sockfd, 0);
                    close(sockfd);
                }
            }

            // 情况 C：网卡空闲了，催我们发货！(EPOLLOUT)
            else if (events[i].events & EPOLLOUT) {
                local_timer.add(sockfd, TIMEOUT_MS, [sockfd, my_epoll_fd]() {
                    LOG_INFO("[无情监工] 客户端: %d 挂机超过15秒，拔网线！", sockfd);
                    epoll_ctl(my_epoll_fd, EPOLL_CTL_DEL, sockfd, 0);
                    close(sockfd);
                });
                
                if (users[sockfd].write()) { // 线程池负责执行发货
                    epoll_event event;
                    event.data.fd = sockfd;
                    if (users[sockfd].get_bytes_to_send() == 0) {
                        users[sockfd].init();
                        event.events = EPOLLIN | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
                    } else {
                        event.events = EPOLLOUT | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
                    }
                    epoll_ctl(my_epoll_fd, EPOLL_CTL_MOD, sockfd, &event);
                } else {
                    epoll_ctl(my_epoll_fd, EPOLL_CTL_DEL, sockfd, 0); 
                    close(sockfd);
                }
            }

        }
    }
}

int main() {
    signal(SIGPIPE, SIG_IGN);

    // 绑定停机信号
    // SIGINT: 对应你在终端按 Ctrl+C
    // SIGTERM: 对应 Docker 发送的 docker stop 指令
    signal(SIGINT, handle_shutdown_signal);
    signal(SIGTERM, handle_shutdown_signal);

    //  1. 读取外部控制面板
    Config::Instance()->Load("./server.conf");

    //  2. 读取并初始化日志
    Log::Instance()->init("./log/tiny_server.log", 1024);
    
    // 读取控制面板里的日志级别，给宏定义当开关用
    GLOBAL_LOG_LEVEL = Config::Instance()->GetInt("log_level", 1);  

    LOG_INFO("\n"); 
    LOG_INFO("==================================================");
    LOG_INFO("========== Tiny Server Rebooting... ==========");
    LOG_INFO("==================================================");
    LOG_INFO("异步日志系统启动成功，大管家已就位！");

    //  3. 读取数据库情报，并启动连接池
    std::string db_host = Config::Instance()->GetString("db_host", "localhost");
    int db_port = Config::Instance()->GetInt("db_port", 3306);
    std::string db_user = Config::Instance()->GetString("db_user", "root");
    std::string db_pwd = Config::Instance()->GetString("db_pwd", "");
    std::string db_name = Config::Instance()->GetString("db_name", "tiny_server");

    LOG_INFO("[System] 引擎就绪，正在连接数据库: %s:%d", db_host.c_str(), db_port);

    LOG_INFO("[System] C++ 战车等待 MySQL 热身 (5秒)...\n");
    fflush(stdout);
    sleep(5);
    
    // 把读取到的配置喂给连接池
    SqlConnPool::Instance()->init(db_host.c_str(), db_port, db_user.c_str(), db_pwd.c_str(), db_name.c_str(), 10);

    global_pool = new ThreadPool(16);
    users = new http_conn[MAX_FD];

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); 

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(8080);       

    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, 1024);

    // 启动 4 个子反应堆线程！
    std::vector<std::thread> sub_threads;
    for (int i = 0; i < SUB_REACTOR_NUM; i++) {
        sub_epoll_fds[i] = epoll_create(5); // 创建子 epoll
        sub_threads.emplace_back(sub_reactor_loop, i); // 启动线程
    }

    // 大堂经理的主循环 (Main Reactor)
    int main_epoll_fd = epoll_create(5);
    add_fd_to_epoll(main_epoll_fd, server_fd, false); // 只监听 server_fd
    
    int round_robin_idx = 0; // 轮询分配计数器
    epoll_event events[MAX_EVENTS];

    LOG_INFO("🚀 高性能 Multi-Reactor 架构已启动");

    while (server_is_running) {
        int number = epoll_wait(main_epoll_fd, events, MAX_EVENTS, -1);

        if (number < 0 && errno == EINTR) continue;

        for (int i = 0; i < number; i++) {
            int sockfd = events[i].data.fd;
            
            // 主线程只干一件事：接客
            if (sockfd == server_fd) {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
                while (true) {
                    int client_fd = accept(server_fd, (struct sockaddr *)&client_address, &client_addrlength);
                    
                    if (client_fd <= 0) {
                        // 如果返回 -1 且错误码是 EAGAIN，说明门外的客人已经全部接完了！
                        // 此时才可以安心 break，回去继续睡大觉等下一次门铃。
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break; 
                        }
                        // 如果是其他严重错误，也退出本次接客
                        break; 
                    }

                    // 正常办理入住手续 (你之前的完美逻辑，完全不用动)
                    users[client_fd].init(client_fd); 

                    int target_sub_epoll = sub_epoll_fds[round_robin_idx];
                    set_non_blocking(client_fd); // 给客人设置非阻塞
                    epoll_event event;
                    event.data.fd = client_fd;
                    event.events = EPOLLIN | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;     
                    
                    epoll_ctl(target_sub_epoll, EPOLL_CTL_ADD, client_fd, &event);
                    
                    // 下一个客人给下一个管家
                    round_robin_idx = (round_robin_idx + 1) % SUB_REACTOR_NUM; 
                }
            }
        }
    }

    close(server_fd); 
    close(main_epoll_fd);
    
    for (auto& t : sub_threads) {
        if (t.joinable()) t.join();
    }

    delete[] users;
    delete global_pool;
    SqlConnPool::Instance()->ClosePool();

    LOG_INFO("[System] 底层资源回收完毕。");
    LOG_INFO("=================================");
    LOG_INFO("[System] 战车已安全熄火。期待下次点火！");
    LOG_INFO("=================================");
    Log::Instance()->flush();

    return 0;
}