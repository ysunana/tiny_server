#ifndef SQL_CONNECTION_POOL_H
#define SQL_CONNECTION_POOL_H

#include <mysql/mysql.h>
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <cassert>
#include "src/log/log.h"

class SqlConnPool {
public:
    // 经典的单例模式：全服共享这一个池子
    static SqlConnPool* Instance();

    // 初始化连接池
    void init(const char* host, int port,
              const char* user, const char* pwd, 
              const char* dbName, int connSize);

    // 工作线程从池子里拿一个连接
    MYSQL* GetConnection();
    
    // 工作线程用完后，把连接还给池子
    void FreeConnection(MYSQL* conn);
    
    // 销毁池子
    void ClosePool();

private:
    SqlConnPool() = default;
    ~SqlConnPool() { ClosePool(); }

    std::queue<MYSQL*> connQue_; // 存放数据库连接的队列
    std::mutex mtx_;             // 互斥锁：防止两个线程抢同一个连接
    std::condition_variable cond_;// 条件变量：池子空了就让线程排队等
};

// =======================================================
// [高级 C++ 技巧] RAII 机制：利用局部变量的生命周期自动归还连接
// 这样工作线程就不怕忘记调用 FreeConnection 导致连接泄露了
// =======================================================
class SqlConnRAII {
public:
    SqlConnRAII(MYSQL** sql, SqlConnPool* connpool) {
        assert(connpool);
        *sql = connpool->GetConnection();
        sql_ = *sql;
        pool_ = connpool;
    }
    ~SqlConnRAII() {
        if (sql_) { pool_->FreeConnection(sql_); }
    }
private:
    MYSQL* sql_;
    SqlConnPool* pool_;
};

#endif