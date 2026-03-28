#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

class ThreadPool {
public:
    ThreadPool(int thread_count = 8);
    ~ThreadPool();
    
    // 往任务队列里加活
    void addTask(std::function<void()> task);

private:
    std::vector<std::thread> workers;   // 工作线程
    std::queue<std::function<void()>> tasks; // 任务队列
    
    std::mutex queue_mutex;            // 保护队列的锁
    std::condition_variable condition; // 线程同步
    bool stop;
};

#endif