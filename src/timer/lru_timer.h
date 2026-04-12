#ifndef LRU_TIMER_H
#define LRU_TIMER_H

#include <chrono>
#include <functional>
#include <list>
#include <vector>
#include <assert.h>

// 类型别名定义，简化代码
typedef std::function<void()> TimeoutCallBack;
typedef std::chrono::high_resolution_clock Clock;
typedef std::chrono::milliseconds MS;
typedef Clock::time_point TimeStamp;

// 定时器节点：记录客人 FD、过期时间和拔网线的回调函数
struct TimerNode {
    int id;
    TimeStamp expires;
    TimeoutCallBack cb;
};

// O(1) 终极定时器核心类
class ListTimer {
public:
    ListTimer();
    ~ListTimer();

    // 添加或更新定时器
    void add(int id, int timeout_ms, const TimeoutCallBack& cb);
    
    // 清理过期连接 (无情的斩杀线)
    void tick();
    
    // 获取距离下一个定时器过期还有多少毫秒
    int getNextTick();

private:
    std::list<TimerNode> nodes_; // 双向链表，自然维护时间先后顺序
    std::vector<std::list<TimerNode>::iterator> ref_; // 哈希数组，通过 FD 直接定位链表节点
};

#endif // LRU_TIMER_H