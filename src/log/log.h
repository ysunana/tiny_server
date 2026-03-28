#ifndef LOG_H
#define LOG_H

#include <mutex>
#include <string>
#include <thread>
#include <sys/time.h>
#include <string.h>
#include <stdarg.h>           // 处理可变参数 (比如 printf 那种)
#include "block_queue.h"      // 引入我们刚写的桌子

class Log {
public:
    // 1. 单例模式：全服只有一个日志大管家
    static Log* Instance() {
        static Log inst;
        return &inst;
    }

    // 2. 开业准备（初始化）：传入日志文件名、桌子大小
    void init(const char* file_name, int max_queue_size = 1024);

    // 3. 厨师写日志（生产者行为）
    void write(int level, const char *format, ...);

    // 4. 强制刷新，把缓冲区立刻写进磁盘
    void flush();

private:
    Log();
    virtual ~Log();

    // 5. 传菜员的专属工作（后台线程真正执行的函数）
    void AsyncWrite_();

private:
    const int MAX_LINES = 50000; // 一个日志文件最多写多少行（超过就建新文件）
    int lineCount_;              // 当前文件写了多少行了
    bool isAsync_;               // 是否开启了异步模式
    
    FILE* fp_;                   // 指向磁盘日志文件的指针
    std::unique_ptr<BlockQueue<std::string>> deq_; // 我们的“大长桌”
    std::unique_ptr<std::thread> writeThread_;     // 那个专门的传菜员（后台线程）
    std::mutex mtx_;             // 保护 fp_ 和内部状态的锁
};

// =========================================================================
// 【高能预警】：全网最爱的“对讲机” (宏定义)
// ##__VA_ARGS__ 是 C++ 的黑魔法，用来支持可变参数（就像 printf 一样）
// =========================================================================
#define LOG_DEBUG(format, ...) Log::Instance()->write(0, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...)  Log::Instance()->write(1, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...)  Log::Instance()->write(2, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) Log::Instance()->write(3, format, ##__VA_ARGS__)

#endif // LOG_H