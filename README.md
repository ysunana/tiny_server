# TinyServer-V12: 高性能 C++ 异步 Web 服务器

[![Language](https://img.shields.io/badge/Language-C++14-blue.svg)](https://en.cppreference.com/w/cpp/14)
[![Crypto](https://img.shields.io/badge/Crypto-OpenSSL-red.svg)](https://www.openssl.org/)
[![Docker](https://img.shields.io/badge/Docker-Supported-blue.svg)](https://www.docker.com/)

TinyServer 是一款基于 C++ 开发的高性能异步 Web 服务器，采用 Reactor 模型，集成了 Redis 旁路缓存与 MySQL 数据库连接池。历经极限高并发压测与底层状态机调优，旨在提供工业级网络韧性与安全性。

## 核心硬核特性 (Highlights)

* **极致网络韧性 (EPOLLOUT 状态机)**：纯手写底层 `EPOLLOUT` 事件驱动与断点续传机制（分散写 `writev`），完美解决操作系统网卡 `EAGAIN` 拥堵问题，实现高并发下 **0 丢包、0 Socket Read Errors**。
* **无状态安全鉴权 (Hardcore JWT)**：摒弃臃肿第三方库，直接调用底层 **OpenSSL (HMAC-SHA256)** 汇编级算力，手撕纯正的 JWT 签发与验票引擎，实现高性能 API 路由拦截。
* **双重存储架构**：
    * **MySQL 连接池**：基于 RAII 机制实现连接回收，解决高并发下的数据库建立连接瓶颈。
    * **Redis 旁路缓存**：通过 `thread_local` 维持线程级长连接，实现缓存极速命中。
* **高性能并发模型**：基于 `Epoll` 边缘触发（ET）模式实现非阻塞 I/O，配合自主实现的条件变量线程池（Thread Pool），压榨多核 CPU 性能。
* **零拷贝技术**：利用 `mmap` 进行静态资源分发，极大提升大文件传输效率。

## 性能表现 (Benchmark)

在 WSL2 (Ubuntu 22.04) Docker 环境下，使用 `wrk` 携带完整 POST 数据流进行极限压力测试（触发 JWT 验票 + Redis/MySQL 链路 + EPOLLOUT 状态机），真实业务场景极限性能如下：

| 测试指标 | 测试结果 | 备注说明 |
| :--- | :--- | :--- |
| **真实业务吞吐量 (QPS)** | **5,248+** | 包含完整 POST 解析、缓存击穿与登录态校验 |
| **平均延迟 (Latency)** | **75ms** | 极速响应 |
| **网络异常率** | **0%** | **Socket Errors: 0** (得益于完美的 Epoll 写状态机) |

> *注：压测命令：`wrk -t4 -c400 -d30s -s post.lua http://127.0.0.1:8080/login`*

## 快速开始 (One-Click Deploy)

项目已提供 `docker-compose` 编排配置，无需手动配置数据库与缓存环境，一键点火部署。

```bash
# 1. 克隆仓库
git clone [https://github.com/ysunana/tiny_server.git](https://github.com/ysunana/tiny_server.git)
cd tiny_server

# 2. 一键启动 (包含 MySQL, Redis 和 C++ Server)
docker compose up -d --build