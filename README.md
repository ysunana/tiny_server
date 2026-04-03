# TinyServer-V12: 高性能 C++ 异步 Web 服务器

[![Language](https://img.shields.io/badge/Language-C++14-blue.svg)](https://en.cppreference.com/w/cpp/14)
[![Build](https://img.shields.io/badge/Build-Bazel-green.svg)](https://bazel.build/)
[![Docker](https://img.shields.io/badge/Docker-Supported-blue.svg)](https://www.docker.com/)

TinyServer 是一款基于 C++ 开发的高性能异步 Web 服务器，采用 Reactor 模型，集成了 Redis 旁路缓存与 MySQL 数据库连接池，旨在处理万级并发请求。

## 核心特性

* **高性能网络模型**：基于 `Epoll` 边缘触发（ET）模式，实现非阻塞 I/O。
* **并发处理**：自主实现线程池（Thread Pool），有效避免线程频繁创建与销毁的系统开销。
* **双重存储架构**：
    * **MySQL 连接池**：基于 RAII 机制实现连接回收，解决高并发下的数据库连接瓶颈。
    * **Redis 旁路缓存**：引入 Redis 缓存用户信息，通过 `thread_local` 维持线程级长连接，将动态请求延迟降低 38% 以上。
* **零拷贝技术**：利用 `mmap` 进行静态资源分发，极大提升文件传输效率。
* **现代化构建与部署**：使用 `Bazel` 构建系统，并提供 `Docker` 一键容器化部署方案。

## 性能表现 (Benchmark)

在 Windows 11 (WSL2 Ubuntu 22.04) 环境下，使用 `wrk` 进行极限压力测试结果如下：

| 测试类型 | 吞吐量 (QPS) | 平均延迟 (Latency) | 成功率 |
| :--- | :--- | :--- | :--- |
| **静态资源 (/index.html)** | **5,600+** | 100ms | 100% |
| **动态登录 (/login - Redis 开)** | **16,400+** | **25ms** | 94% (网卡极限) |

> *注：压测命令：`wrk -t4 -c400 -d30s -s post.lua http://127.0.0.1:8080/login`*

## 项目架构



1.  **主线程**：负责 `epoll_wait` 监听 `listenfd` 和 `connfd`。
2.  **工作线程**：从线程池获取任务，处理 HTTP 解析、数据库查询及数据回写。
3.  **连接管理**：采用定时器（Timer）机制自动清理非活跃连接。

## 快速开始

### 依赖环境
* Docker & Docker Compose
* Git

### 一键部署
```bash
# 1. 克隆仓库
git clone [https://github.com/ysunana/tiny_server.git](https://github.com/ysunana/tiny_server.git)
cd tiny_server

# 2. 一键启动 (包含 MySQL, Redis 和 Server)
docker-compose up -d