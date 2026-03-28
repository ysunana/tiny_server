#include "sql_connection_pool.h"
#include <iostream>
#include <assert.h>

SqlConnPool* SqlConnPool::Instance() {
    static SqlConnPool connPool;
    return &connPool;
}

void SqlConnPool::init(const char* host, int port,
                       const char* user, const char* pwd, 
                       const char* dbName, int connSize) {
    for (int i = 0; i < connSize; i++) {
        // 调用 MySQL C API 初始化结构体
        MYSQL *sql = nullptr;
        sql = mysql_init(sql);
        if (!sql) {
            LOG_ERROR("[DB] MySQL 初始化失败!");
            assert(sql);
        }
        
        // 真正发起网络请求，连接到 MySQL 服务器
        MYSQL *res = mysql_real_connect(sql, host, user, pwd, dbName, port, nullptr, 0);
        if (!res) {
            // 用 mysql_error() 打印出真正的死因！
            LOG_ERROR("[DB] MySQL 连接失败! 真实原因: %s ", mysql_error(sql));
            assert(res); // 发现失败，立刻让程序崩溃停止，绝对不能假装成功！
        }
        
        // 连上之后，扔进我们的队列里备用
        connQue_.push(sql);
    }
    LOG_INFO("[DB] 成功建立 %d 个 MySQL 预备连接。", connSize);
}

MYSQL* SqlConnPool::GetConnection() {
    MYSQL* sql = nullptr;
    std::unique_lock<std::mutex> lock(mtx_);
    
    // 如果池子空了，就让线程在这里阻塞等待，直到有人还连接
    while (connQue_.empty()) {
        cond_.wait(lock);
    }
    
    sql = connQue_.front();
    connQue_.pop();
    return sql;
}

void SqlConnPool::FreeConnection(MYSQL* sql) {
    assert(sql);
    std::unique_lock<std::mutex> lock(mtx_);
    connQue_.push(sql);
    cond_.notify_one(); // 唤醒一个可能在等待连接的线程
}

void SqlConnPool::ClosePool() {
    std::unique_lock<std::mutex> lock(mtx_);
    while (!connQue_.empty()) {
        auto item = connQue_.front();
        connQue_.pop();
        mysql_close(item); // 断开与 MySQL 引擎的连接
    }
}