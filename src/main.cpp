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

    // 在程序刚苏醒的第一时间启动异步日志！
    // 参数1：日志文件名  参数2：阻塞队列最大长度（大于0代表开启异步传菜员模式）
    // Log::Instance()->init("./tiny_server.log", 1024);

    // LOG_INFO("\n"); 
    // LOG_INFO("==================================================");
    // LOG_INFO("========== Tiny Server Rebooting... ==========");
    // LOG_INFO("==================================================");
    // LOG_INFO("异步日志系统启动成功，大管家已就位！");

    SqlConnPool::Instance()->init("host.docker.internal", 3306, "webuser", "123456", "tiny_server", 10);

    ThreadPool pool(16); 
    HeapTimer timer; // 创建一个无情的监工

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
        // 每次循环前，问一下定时器：最近的一个死期还有多久？
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
                    
                    timer.add(client_fd, TIMEOUT_MS, [client_fd, epoll_fd]() {
                        // LOG_INFO("[无情监工] 客户端: %d 挂机超过15秒，拔网线！", client_fd);
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, 0); 
                        close(client_fd);                                
                    });
                }
            } 

            // 情况 B：客人发请求来了 (EPOLLIN)
            else if (events[i].events & EPOLLIN) {
                // 再次调用 add 时，定时器内部会找到它并自动下沉/上浮调整堆的位置
                timer.add(sockfd, TIMEOUT_MS, [sockfd, epoll_fd]() {
                    // LOG_INFO("[无情监工] 客户端: %d 挂机超过15秒，拔网线！", sockfd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sockfd, 0);
                    close(sockfd);
                });

                // 把处理任务丢给线程池
                pool.addTask([sockfd, epoll_fd, users] {
                    users[sockfd].process(); // 1. 解析请求并装配好小本本
                    
                    // 2. 无论有没有解析出数据，立刻强制切换到发货模式 (EPOLLOUT)！
                    epoll_event event;
                    event.data.fd = sockfd;
                    event.events = EPOLLOUT | EPOLLET | EPOLLONESHOT;
                    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, sockfd, &event);
                });
            }

            // 情况 C：网卡空闲了，催我们发货！(EPOLLOUT)
            else if (events[i].events & EPOLLOUT) {
                timer.add(sockfd, TIMEOUT_MS, [sockfd, epoll_fd]() {
                    // LOG_INFO("[无情监工] 客户端: %d 挂机超过15秒，拔网线！", sockfd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sockfd, 0);
                    close(sockfd);
                });
                
                pool.addTask([sockfd, epoll_fd, users] {
                    bool keep_alive = users[sockfd].write(); // 开始干苦力发货！
                    
                    if (keep_alive) {
                        epoll_event event;
                        event.data.fd = sockfd;
                        
                        if (users[sockfd].get_bytes_to_send() == 0) {
                            // 货全发完了，恢复到接客模式，等客人下一次发消息
                            event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                        } else {
                            // 货没发完被网卡弹回来了，继续保持发货模式死等
                            event.events = EPOLLOUT | EPOLLET | EPOLLONESHOT;
                        }
                        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, sockfd, &event);
                    } else {
                        // 短连接或者发生严重错误，拔网线
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sockfd, 0); 
                        close(sockfd);
                    }
                });
            }
        }
    }
    return 0;
}