#include "log.h"

Log::Log() : lineCount_(0), isAsync_(false), fp_(nullptr), deq_(nullptr), writeThread_(nullptr) {}

Log::~Log() {
    if(writeThread_ && writeThread_->joinable()) {
        while(!deq_->empty()) {
            deq_->flush(); // 等传菜员把桌上剩下的菜全端走
        }
        deq_->Close();     // 掀桌子
        writeThread_->join(); // 等待传菜员体面下班
    }
    if(fp_) {
        std::lock_guard<std::mutex> locker(mtx_);
        flush();
        fclose(fp_);
    }
}

// ==========================================
// 【核心逻辑 1】：开业大吉 (生成文件，雇佣传菜员)
// ==========================================
void Log::init(const char* file_name, int max_queue_size) {
    isAsync_ = false;
    // 如果设置了长桌子的大小，说明要开启异步模式！
    if(max_queue_size > 0) {
        isAsync_ = true;
        deq_ = std::make_unique<BlockQueue<std::string>>(max_queue_size);
        
        // 【最惊艳的一行】：直接雇佣一个后台线程！
        // 它的工作就是死循环执行 AsyncWrite_ 函数
        writeThread_ = std::make_unique<std::thread>(&Log::AsyncWrite_, this);
    }

    // 打开（或创建）磁盘上的日志文件，准备写入 ("a" 代表追加)
    fp_ = fopen(file_name, "a");
    if(fp_ == nullptr) {
        printf("[Log] 日志文件打开失败！\n");
    }
}

// ==========================================
// 【核心逻辑 2】：传菜员的死循环 (消费者)
// ==========================================
void Log::AsyncWrite_() {
    std::string str = "";
    // 死循环死死盯着大长桌！
    while(deq_->pop(str)) { // 如果桌上没菜，他就会在这里睡着（被条件变量阻塞）
        std::lock_guard<std::mutex> locker(mtx_);
        // 只要拿到菜，就立刻写进磁盘！
        fputs(str.c_str(), fp_);
        fflush(fp_);
    }
}

// ==========================================
// 【核心逻辑 3】：厨师炒菜 (生产者)
// ==========================================
void Log::write(int level, const char *format, ...) {
    struct timeval now = {0, 0};
    gettimeofday(&now, nullptr);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    char s[16] = {0};
    switch(level) {
        case 0: strcpy(s, "[DEBUG]:"); break;
        case 1: strcpy(s, "[INFO]: "); break;
        case 2: strcpy(s, "[WARN]: "); break;
        case 3: strcpy(s, "[ERROR]:"); break;
        default:strcpy(s, "[INFO]: "); break;
    }

    // 这块是为了把 时间、日志级别、具体内容 拼成一个完整的字符串
    char log_str[1024] = {0};
    va_list valst;
    va_start(valst, format);
    
    // 拼装时间头 (比如: 2026-03-25 10:00:00.123456 [INFO]: )
    int n = snprintf(log_str, 256, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    
    // 拼装真正的日志内容 (比如: "客人 Sunana 登录了")
    int m = vsnprintf(log_str + n, 1024 - 1 - n, format, valst);
    log_str[n + m] = '\n';
    log_str[n + m + 1] = '\0';
    va_end(valst);

    std::string final_log_str = log_str;

    if(isAsync_ && deq_ && !deq_->full()) {
        // 【高光时刻】：如果是异步模式，且桌子没满，直接扔给大长桌！
        // 扔完厨师直接就走人了，绝对不在这里等磁盘的龟速 I/O！
        deq_->push(final_log_str);
    } else {
        // 如果没开异步，或者桌子满了，厨师只好自己苦逼地去写磁盘了
        std::lock_guard<std::mutex> locker(mtx_);
        fputs(final_log_str.c_str(), fp_);
    }
}

void Log::flush() {
    if(isAsync_) { 
        deq_->flush(); 
    }
    fflush(fp_);
}