#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/epoll.h> 
#include <signal.h>

#include "src/threadpool/threadpool.h"
#include "src/http/http_conn.h"
#include "src/timer/heaptimer.h" 
#include "src/mysql/sql_connection_pool.h"
#include "src/log/log.h"

#define MAX_EVENTS 1024
#define TIMEOUT_MS 15000 // 15秒无操作直接踢出！
#define MAX_FD 65536

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
        event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
    } else {
        // 前台接待员 (server_fd)：绝不能加 EPOLLONESHOT，他必须永远醒着！
        event.events = EPOLLIN | EPOLLET;
    }
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
    set_non_blocking(fd);
}

int main() {
    signal(SIGPIPE, SIG_IGN);

    // 【新增这行神仙代码】：在程序刚苏醒的第一时间启动异步日志！
    // 参数1：日志文件名  参数2：阻塞队列最大长度（大于0代表开启异步传菜员模式）
    // Log::Instance()->init("./tiny_server.log", 1024);

    // LOG_INFO("\n"); 
    // LOG_INFO("==================================================");
    // LOG_INFO("========== 🚀 Tiny Server Rebooting... ==========");
    // LOG_INFO("==================================================");
    // LOG_INFO("异步日志系统启动成功，大管家已就位！");

    SqlConnPool::Instance()->init("host.docker.internal", 3306, "webuser", "123456", "tiny_server", 10);

    ThreadPool pool(16); 
    HeapTimer timer; // 【新增】创建一个无情的监工

    http_conn* users = new http_conn[MAX_FD];

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); 

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(8080);       

    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, 1024);

    int epoll_fd = epoll_create(5); 
    add_fd_to_epoll(epoll_fd, server_fd, false);

    // LOG_INFO("🚀 高性能服务器已启动 (搭载 15s 超时踢人机制)");

    epoll_event events[MAX_EVENTS]; 

    while (true) {
        // 【核心修改】每次循环前，问一下定时器：最近的一个死期还有多久？
        // 如果返回 -1，说明目前没人连着，epoll 就可以安心睡大觉死等。
        int time_ms = timer.getNextTick();
        
        // 把 time_ms 喂给 epoll_wait。如果过了 time_ms 还没人发数据，它会自动醒来！
        int number = epoll_wait(epoll_fd, events, MAX_EVENTS, time_ms);

        for (int i = 0; i < number; i++) {
            int sockfd = events[i].data.fd;

            // 情况 A：有新客人
            if (sockfd == server_fd) {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
                while (true) {
                    int client_fd = accept(server_fd, (struct sockaddr *)&client_address, &client_addrlength);
                    
                    if (client_fd < 0) {
                        // 如果返回 -1 且错误码是 EAGAIN，说明内核队列已经被彻底掏空了！
                        // 所有人都在屋里了，安心退出循环，回去等下一次 Epoll 通知。
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;
                        }
                        // 其他错误暂且跳过
                        break; 
                    }
                    
                    // 当有新客户端连入 (accept) 时，直接把这个 socket 喂给对应的管家初始化：
                    users[client_fd].init(client_fd);

                    // 只要拿到了客人，就扔进 Epoll 和定时器
                    add_fd_to_epoll(epoll_fd, client_fd, true);
                    // LOG_INFO("[Epoll] 新客人连入: %d", client_fd);
                    
                    // 登记生死簿 (这里我把你原来的日志注释掉了，保持静音加速！)
                    timer.add(client_fd, TIMEOUT_MS, [client_fd, epoll_fd]() {
                        // LOG_INFO("[无情监工] 客户端: %d 挂机超过15秒，拔网线！", client_fd);
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, 0); 
                        close(client_fd);                                
                    });
                }
            } 

            // 情况 B：老客人发请求了！
            else if (events[i].events & EPOLLIN) {
                // 【新增】客人有动静了，表现很好，给他“续命” 15 秒！
                // 再次调用 add 时，定时器内部会找到它并自动下沉/上浮调整堆的位置
                timer.add(sockfd, TIMEOUT_MS, [sockfd, epoll_fd]() {
                    // LOG_INFO("[无情监工] 客户端: %d 挂机超过15秒，拔网线！", sockfd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sockfd, 0);
                    close(sockfd);
                });

                // 把处理任务丢给线程池
                pool.addTask([sockfd, epoll_fd, users] {
                    users[sockfd].process(); // 这个管家自带持续的 read_buf，绝不失忆！
                    
                    // 【修改】：核心裁决逻辑！查小本本！
                    if (users[sockfd].is_keep_alive()) {
                        epoll_event event;
                        event.data.fd = sockfd;
                        event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, sockfd, &event);
                        // LOG_INFO("客户端 %d 开启长连接，等待下一次请求...", sockfd);
                    } else {
                        // 短连接：客人是一次性访问，或者不符合长连接规矩。
                        // 绝不浪费资源，立刻、当场、马上拔网线！
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sockfd, 0); 
                        close(sockfd);
                        // LOG_INFO("客户端 %d 短连接结束，已清理。", sockfd);
                    }
                });
            }
        }
    }
    return 0;
}