#ifndef HEAP_TIMER_H
#define HEAP_TIMER_H

#include <vector>
#include <unordered_map>
#include <functional>
#include <chrono>
#include <iostream>

// 定义一个回调函数类型，用来执行“拔网线”的操作
typedef std::function<void()> TimeoutCallBack;

using Clock = std::chrono::high_resolution_clock;
using MS = std::chrono::milliseconds;
using TimeStamp = Clock::time_point;

// 定时器节点
struct TimerNode {
    int id;             // 也就是 Socket 的 FD
    TimeStamp expires;  // 这个连接的“死期” (绝对时间)
    TimeoutCallBack cb; // 到期后要执行的动作 (比如 close(fd))

    // 重载小于号，为了方便在堆里比较大小
    bool operator<(const TimerNode& t) const {
        return expires < t.expires;
    }
};

class HeapTimer {
public:
    HeapTimer() { heap_.reserve(64); }
    ~HeapTimer() { heap_.clear(); ref_.clear(); }

    // 添加新连接，或者给老连接更新过期时间
    void add(int id, int timeout_ms, const TimeoutCallBack& cb);
    
    // 清理红黑树中所有已经过期的连接
    void tick();
    
    // 【核心】计算距离堆顶元素过期还剩多少毫秒，用来喂给 epoll_wait
    int getNextTick();

private:
    // 堆的底层操作：上浮、下沉、交换
    void siftup_(size_t i);
    bool siftdown_(size_t index, size_t n);
    void SwapNode_(size_t i, size_t j);

    std::vector<TimerNode> heap_;         // 用数组模拟完全二叉树
    std::unordered_map<int, size_t> ref_; // 映射：fd -> 它在 heap_ 数组中的下标
};

#endif