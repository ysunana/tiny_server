#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <mutex>
#include <deque>
#include <condition_variable>
#include <sys/time.h>
#include <cassert>

template<class T>
class BlockQueue {
public:
    explicit BlockQueue(size_t maxCapacity = 1000); // 默认最多存 1000 条未写的日志
    ~BlockQueue();

    void clear();
    bool empty();
    bool full();
    void Close();
    size_t size();
    size_t capacity();
    
    T front();
    T back();

    // 厨师扔菜（生产者）
    void push(const T &item);
    
    // 传菜员端菜（消费者）
    bool pop(T &item);
    bool pop(T &item, int timeout); // 带超时的 pop，防止死锁
    void flush();

private:
    std::deque<T> deq_;                      // 底层用双端队列存数据
    size_t capacity_;                        // 队列最大容量（防止内存雪崩）
    std::mutex mtx_;                         // 互斥锁
    bool isClose_;                           // 队列是否已关闭
    std::condition_variable condConsumer_;   // 唤醒消费者的条件变量（传菜员）
    std::condition_variable condProducer_;   // 唤醒生产者的条件变量（厨师）
};

template<class T>
BlockQueue<T>::BlockQueue(size_t maxCapacity) : capacity_(maxCapacity) {
    assert(maxCapacity > 0);
    isClose_ = false;
}

template<class T>
BlockQueue<T>::~BlockQueue() {
    Close();
}

template<class T>
void BlockQueue<T>::Close() {
    {
        std::lock_guard<std::mutex> locker(mtx_);
        deq_.clear();
        isClose_ = true;
    }
    // 队列关闭时，唤醒所有正在沉睡的线程，让它们体面地退出
    condProducer_.notify_all();
    condConsumer_.notify_all();
}

template<class T>
void BlockQueue<T>::flush() {
    condConsumer_.notify_one();
}

template<class T>
void BlockQueue<T>::clear() {
    std::lock_guard<std::mutex> locker(mtx_);
    deq_.clear();
}

template<class T>
bool BlockQueue<T>::empty() {
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.empty();
}

template<class T>
bool BlockQueue<T>::full() {
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.size() >= capacity_;
}

template<class T>
T BlockQueue<T>::front() {
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.front();
}

template<class T>
T BlockQueue<T>::back() {
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.back();
}

template<class T>
size_t BlockQueue<T>::size() {
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.size();
}

template<class T>
size_t BlockQueue<T>::capacity() {
    std::lock_guard<std::mutex> locker(mtx_);
    return capacity_;
}

// ==========================================
// 【核心逻辑】：生产者 push (工作线程写日志)
// ==========================================
template<class T>
void BlockQueue<T>::push(const T &item) {
    std::unique_lock<std::mutex> locker(mtx_);
    // 如果桌子满了，厨师就得等着！(保护内存)
    while(deq_.size() >= capacity_) {
        condProducer_.wait(locker);
    }
    deq_.push_back(item);
    // 扔完菜，摇铃铛叫醒传菜员！
    condConsumer_.notify_one();
}

// ==========================================
// 【核心逻辑】：消费者 pop (后台线程写磁盘)
// ==========================================
template<class T>
bool BlockQueue<T>::pop(T &item) {
    std::unique_lock<std::mutex> locker(mtx_);
    // 如果桌子是空的，传菜员就去睡觉！
    while(deq_.empty()) {
        condConsumer_.wait(locker);
        if(isClose_) {
            return false;
        }
    }
    item = deq_.front();
    deq_.pop_front();
    // 拿走一盘菜，桌子腾出空间了，叫醒可能正在等桌子的厨师！
    condProducer_.notify_one();
    return true;
}

// 带超时的 pop（工业级必备，防止线程卡死）
template<class T>
bool BlockQueue<T>::pop(T &item, int timeout) {
    std::unique_lock<std::mutex> locker(mtx_);
    while(deq_.empty()) {
        if(condConsumer_.wait_for(locker, std::chrono::seconds(timeout)) 
                == std::cv_status::timeout) {
            return false;
        }
        if(isClose_) {
            return false;
        }
    }
    item = deq_.front();
    deq_.pop_front();
    condProducer_.notify_one();
    return true;
}

#endif // BLOCK_QUEUE_H