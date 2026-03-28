#include "threadpool.h"
#include <iostream>

// 构造函数：初始化线程池
ThreadPool::ThreadPool(int thread_count) : stop(false) {
    // 循环创建 thread_count 条线程
    for (int i = 0; i < thread_count; ++i) {
        // 每条线程执行一个 lambda 表达式（即线程的死循环逻辑）
        workers.emplace_back([this] {
            while (true) {
                std::function<void()> task;

                // --- 临界区开始：从队列取任务 ---
                {
                    // 1. 获取锁，确保同一时间只有一个线程在动任务队列
                    std::unique_lock<std::mutex> lock(this->queue_mutex);

                    // 2. 等待条件：如果队列为空且没停止，线程就“睡”在这里，释放 CPU 占用
                    // 只有当有新任务（notify）或线程池停止时，才会被唤醒
                    this->condition.wait(lock, [this] {
                        return this->stop || !this->tasks.empty();
                    });

                    // 3. 如果收到停止信号且队列空了，线程直接退出循环（自毁）
                    if (this->stop && this->tasks.empty()) {
                        return;
                    }

                    // 4. 从队列头部取出一个任务
                    task = std::move(this->tasks.front());
                    this->tasks.pop();
                } 
                // --- 临界区结束：释放锁，让其他线程也能取任务 ---

                // 5. 执行具体的任务逻辑（此时不拿锁，多个线程可以并行干活）
                task();
            }
        });
    }
}

// 往池子里丢任务
void ThreadPool::addTask(std::function<void()> task) {
    {
        // 加锁，保证往队列塞任务时是线程安全的
        std::unique_lock<std::mutex> lock(queue_mutex);
        if (stop) throw std::runtime_error("无法在已停止的线程池中添加任务");
        tasks.emplace(std::move(task));
    }
    // 唤醒一个正在“睡觉”等待任务的工作线程
    condition.notify_one();
}

// 析构函数：优雅地关闭线程池
ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    // 唤醒所有正在睡觉的线程，告诉它们该下班（退出）了
    condition.notify_all();
    
    // 等待所有线程执行完手头的工作并安全退出
    for (std::thread &worker : workers) {
        worker.join();
    }
}