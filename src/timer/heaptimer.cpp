#include "heaptimer.h"
#include <assert.h>

// 交换两个节点，同时别忘了更新哈希表里的下标记录！
void HeapTimer::SwapNode_(size_t i, size_t j) {
    assert(i >= 0 && i < heap_.size());
    assert(j >= 0 && j < heap_.size());
    std::swap(heap_[i], heap_[j]);
    ref_[heap_[i].id] = i;
    ref_[heap_[j].id] = j;
}

// 向上调整 (上浮)：当新节点的时间比父节点更小(更早过期)时
void HeapTimer::siftup_(size_t i) {
    assert(i >= 0 && i < heap_.size());
    size_t j = (i - 1) / 2; // 找爸爸
    while (j >= 0) {
        if (heap_[j] < heap_[i]) { break; } // 爸爸比我小，不用浮了
        SwapNode_(i, j);
        i = j;
        j = (i - 1) / 2;
    }
}

// 向下调整 (下沉)
bool HeapTimer::siftdown_(size_t index, size_t n) {
    assert(index >= 0 && index < heap_.size());
    assert(n >= 0 && n <= heap_.size());
    size_t i = index;
    size_t j = i * 2 + 1; // 找左孩子
    while (j < n) {
        if (j + 1 < n && heap_[j + 1] < heap_[j]) j++; // 选左右孩子里最小的那个
        if (heap_[i] < heap_[j]) break;
        SwapNode_(i, j);
        i = j;
        j = i * 2 + 1;
    }
    return i > index; // 返回是否真的发生了下沉
}

// 添加或更新定时器
void HeapTimer::add(int id, int timeout_ms, const TimeoutCallBack& cb) {
    assert(id >= 0);
    size_t i;
    if (ref_.count(id) == 0) {
        // 新连接：放进数组尾部，然后上浮
        i = heap_.size();
        ref_[id] = i;
        heap_.push_back({id, Clock::now() + MS(timeout_ms), cb});
        siftup_(i);
    } else {
        // 老连接：更新时间，然后调整位置 (先尝试下沉，不行再尝试上浮)
        i = ref_[id];
        heap_[i].expires = Clock::now() + MS(timeout_ms);
        heap_[i].cb = cb;
        if (!siftdown_(i, heap_.size())) {
            siftup_(i);
        }
    }
}

// 清除所有过期的定时器 (无情的斩杀线)
void HeapTimer::tick() {
    if (heap_.empty()) return;
    while (!heap_.empty()) {
        TimerNode node = heap_.front();
        // 如果堆顶的时间还没到当前时间，说明后面的更没到，直接退出
        if (std::chrono::duration_cast<MS>(node.expires - Clock::now()).count() > 0) {
            break; 
        }
        // 执行回调函数 (拔网线！)
        node.cb();
        
        // 把堆顶和最后一个元素交换，然后删掉最后一个，再让新的堆顶下沉
        SwapNode_(0, heap_.size() - 1);
        ref_.erase(heap_.back().id);
        heap_.pop_back();
        if (!heap_.empty()) {
            siftdown_(0, heap_.size());
        }
    }
}

// 获取距离下一个定时器过期还有多少毫秒
int HeapTimer::getNextTick() {
    tick(); // 顺手清理一下
    int res = -1;
    if (!heap_.empty()) {
        res = std::chrono::duration_cast<MS>(heap_.front().expires - Clock::now()).count();
        if (res < 0) res = 0;
    }
    return res;
}