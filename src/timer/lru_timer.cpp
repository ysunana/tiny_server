#include "lru_timer.h"

ListTimer::ListTimer() {
    // 战车初始化：直接一次性分配 65536 个槽位，用套接字 FD 作为绝对索引。
    // 初始状态下，所有指针都指向链表的 end()，表示该 FD 尚未连接。
    ref_.resize(65536, nodes_.end()); 
}

ListTimer::~ListTimer() {
    nodes_.clear();
    ref_.clear();
}

void ListTimer::add(int id, int timeout_ms, const TimeoutCallBack& cb) {
    assert(id >= 0 && id < 65536);
    
    auto it = ref_[id];
    if (it != nodes_.end()) {
        // 更新过期时间，然后直接把它从链表中间“剪切 (splice)”到链表尾部！
        it->expires = Clock::now() + MS(timeout_ms);
        it->cb = cb;
        nodes_.splice(nodes_.end(), nodes_, it); 
    } else {
        // 直接插到尾部，并记录它的内存位置 (iterator)
        nodes_.push_back({id, Clock::now() + MS(timeout_ms), cb});
        ref_[id] = std::prev(nodes_.end());
    }
}

void ListTimer::tick() {
    if (nodes_.empty()) return;
    
    while (!nodes_.empty()) {
        auto& head = nodes_.front(); // 队头永远是最早过期的
        
        // 算出队头还有多久过期，如果 > 0 说明还没到死期
        if (std::chrono::duration_cast<MS>(head.expires - Clock::now()).count() > 0) {
            break; // 连队头都没过期，后面的更不可能过期
        }
        
        // 时间到了，执行回调函数（拔网线）
        head.cb(); 
        
        // 抹除他在索引数组里的痕迹
        ref_[head.id] = nodes_.end(); 
        
        nodes_.pop_front(); 
    }
}

int ListTimer::getNextTick() {
    tick(); // 顺手清理一下
    int res = -1;
    if (!nodes_.empty()) {
        res = std::chrono::duration_cast<MS>(nodes_.front().expires - Clock::now()).count();
        if (res < 0) res = 0;
    }
    return res;
}