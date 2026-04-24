#ifndef SQLCONNPOOL_H
#define SQLCONNPOOL_H

#include <queue>
#include <mutex>
#include <string>
#include <cassert>
#include <semaphore.h>
#include <mysql/mysql.h>


class SqlConnPool {
public:
    // 获取全局唯一连接池实例
    static SqlConnPool* Instance();

    // 初始化连接池：预先创建 connSize 个 MySQL 连接
    // host/ip：数据库地址，port：端口
    // user/pwd：账号密码，dbName：数据库名
    void Init(const char* host,
              int         port,
              const char* user,
              const char* pwd,
              const char* dbName,
              int         connSize);

    // 取一个连接：
    // 若池子暂时无空闲连接，会阻塞等待直到有连接可用
    MYSQL* GetConn();

    // 归还一个连接到池子
    void FreeConn(MYSQL* conn);

    // 关闭连接池，释放所有连接
    void ClosePool();

    // 当前空闲连接数（调试/监控用）
    int GetFreeConnCount();

private:
    SqlConnPool();
    ~SqlConnPool();

    // 单例不允许拷贝
    SqlConnPool(const SqlConnPool&)               = delete;
    SqlConnPool& operator = (const SqlConnPool&)  = delete;

private:
    int MAX_CONN_;   // 连接池容量（总连接数）
    int useCount_;   // 正在被业务线程使用的连接数
    int freeCount_;  // 当前空闲连接数

    std::queue<MYSQL*> connQue_;  // 空闲连接队列
    std::mutex         mtx_;      // 保护连接队列和计数器
    sem_t              semId_;    // 信号量：记录“可取连接”的数量

    // 保存初始化参数（便于排错/日志输出）
    std::string host_;
    int         port_;
    std::string user_;
    std::string pwd_;
    std::string dbName_;
};

#endif
