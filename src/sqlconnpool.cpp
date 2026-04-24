#include "sqlconnpool.h"

#include <cerrno>

// SqlConnPool 构造 / 析构
SqlConnPool::SqlConnPool()
    : MAX_CONN_(0),
      useCount_(0),
      freeCount_(0),
      port_(0) {}

SqlConnPool::~SqlConnPool() {
    ClosePool();
}

// Instance：返回全局唯一实例（单例）
SqlConnPool* SqlConnPool::Instance() {
    static SqlConnPool pool;
    return &pool;
}

// Init：初始化连接池，预创建 connSize 个连接
void SqlConnPool::Init( const char* host,
                        int         port,
                        const char* user,
                        const char* pwd,
                        const char* dbName,
                        int         connSize) {
    assert(host && user && pwd && dbName);
    assert(connSize > 0);
    
    // 如果之前初始化过，先清理旧池子
    if (MAX_CONN_ > 0) {
        ClosePool();
    }

    host_   = host;
    port_   = port;
    user_   = user;
    pwd_    = pwd;
    dbName_ = dbName;

    for (int i = 0; i < connSize; ++i) {
        MYSQL* sql = mysql_init(nullptr);
        assert(sql);

        // 连接失败就关掉该句柄，继续尝试创建下一条连接
        if (!mysql_real_connect(sql, host, user, pwd, dbName, port, nullptr, 0)) {
            mysql_close(sql);
            continue;
        }
        connQue_.push(sql);
    }

    freeCount_ = static_cast<int>(connQue_.size());
    useCount_  = 0;
    MAX_CONN_  = freeCount_;
    assert(MAX_CONN_ > 0);

    // 信号量初值 = 空闲连接数
    assert(sem_init(&semId_, 0, static_cast<unsigned int>(freeCount_)) == 0);

}

// GetConn：取一个连接（无可用连接则阻塞等待
MYSQL* SqlConnPool::GetConn() {
    if (MAX_CONN_ == 0) return nullptr;

    // 被信号打断则重试，其他错误直接返回 nullptr
    while (sem_wait(&semId_) == -1) {
        if (errno != EINTR) {
            return nullptr;
        }
    }

    std::lock_guard<std::mutex> locker(mtx_);
    if (connQue_.empty()) {
        // 理论上不该发生，回滚信号量避免计数错乱
        sem_post(&semId_);
        return nullptr;
    }

    MYSQL* sql = connQue_.front();
    connQue_.pop();

    --freeCount_;
    ++useCount_;
    return sql;
}

// FreeConn：归还连接到连接池
void SqlConnPool::FreeConn(MYSQL* conn) {
    if (!conn) return;

    {
        std::lock_guard<std::mutex> locker(mtx_);
        connQue_.push(conn);
        ++freeCount_;
        if (useCount_ > 0) {
            --useCount_;
        }
    }

    sem_post(&semId_);
}

// ClosePool：关闭连接池，释放所有空闲连接
void SqlConnPool::ClosePool() {
    {
    std::lock_guard<std::mutex> locker(mtx_);
    if (MAX_CONN_ == 0) return;

    while (!connQue_.empty()) {
        MYSQL* sql = connQue_.front();
        connQue_.pop();
        mysql_close(sql);
    }

    freeCount_ = 0;
    useCount_  = 0;
    MAX_CONN_  = 0;

    sem_destroy(&semId_);
    }

    mysql_library_end();
}

// GetFreeConnCount：获取当前空闲连接数
int SqlConnPool::GetFreeConnCount() {
    std::lock_guard<std::mutex> locker(mtx_);
    return freeCount_;
}