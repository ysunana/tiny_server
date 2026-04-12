# V12 High-Performance C++ Web Server

![C++](https://img.shields.io/badge/C++-11%2B-blue.svg) ![Linux](https://img.shields.io/badge/OS-Linux-orange.svg) ![License](https://img.shields.io/badge/License-MIT-green.svg)

基于 C++11 编写的纯正工业级、高性能、高并发 Web 服务器。从零手撕底层网络协议，历经多次架构演进与极限压测调优，单机并发能力突破 **~30,000 QPS**，网络吞吐量拉满千兆网卡物理极限 (**~100 MB/s**)。

## 核心架构演进 (Architecture Highlights)

本项目深度剥离网络 IO 与业务计算，采用 **Half-Sync / Half-Async (半同步/半异步) Multi-Reactor 模型**：

* **Multi-Reactor (主从反应堆)**：
    * **Main Reactor**：主线程专职接客（`accept`），采用死循环榨干全连接队列，彻底解决 `EPOLLET` 模式下的饿死问题。
    * **Sub-Reactors**：多线程子反应堆独立拥有 `epoll` 实例，负责非阻塞 IO 的精准搬运，彻底解决跨线程的 FD 数据竞态 (Double-Close) 死锁。
* **O(1) 终极 LRU 定时器**：摒弃传统 $O(\log N)$ 的最小堆，利用 `std::list` + `std::vector` 手搓 $O(1)$ 复杂度的双向链表定时器，彻底根除 2.5 万并发下的 CPU Cache 颠簸。
* **防御级网络边界处理**：
    * **TCP 半包/粘包截断防线**：完善的状态机解析，精准控制 `EPOLLIN` 与 `EPOLLOUT` 的切换时机，零错误率抗住高频并发轰炸。
    * **优雅停机 (Graceful Shutdown)**：拦截 `SIGINT`/`SIGTERM` 信号，确保缓存落盘与文件句柄安全释放。
    * 引入 `EPOLLRDHUP` 斩击机制，毫秒级清理对端断开的死连接，内存零泄露。

## 极限性能战报 (Benchmark)

使用 `wrk` 工具进行 4 线程 100 并发连接的极限压测，全量开启 Keep-Alive 长连接：

```text
4 threads and 100 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     3.68ms    3.43ms  89.45ms   95.59%
    Req/Sec     6.73k     1.00k    7.94k    96.29%
  273165 requests in 9.24s, 0.92GB read
  Socket errors: connect 0, read 198, write 0, timeout 94
Requests/sec:  29572.88
Transfer/sec:    101.62MB

# 1. 一键编译并启动
sudo docker compose up -d --build

# 2. 实时查看监控日志
sudo docker compose logs -f web_server