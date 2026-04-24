#ifndef SQLCONNRAII_H
#define SQLCONNRAII_H

#include "sqlconnpool.h"

// ================================================================
// SqlConnRAII：MySQL 连接的作用域管理器
// 构造时自动从连接池拿连接，析构时自动归还
// ================================================================
class SqlConnRAII {
public:
    // 构造：自动取连接
    // sql 是外部 MYSQL* 的地址，便于把取到的连接回传给调用方
    SqlConnRAII(MYSQL** sql, SqlConnPool* connPool)
        : sql_(nullptr), connPool_(connPool) {
        assert(sql);
        if (connPool_) {
            *sql = connPool_->GetConn();
            sql_ = *sql;
        } else {
            *sql = nullptr;
        }
    }

    // 析构：自动还连接
    ~SqlConnRAII() {
        if (sql_ && connPool_) {
            connPool_->FreeConn(sql_);
            sql_ = nullptr;
        }
    }

    // 禁拷贝/禁移动，避免同一连接被重复归还
    SqlConnRAII(const SqlConnRAII&)                = delete;
    SqlConnRAII& operator=(const SqlConnRAII&)     = delete;
    SqlConnRAII(SqlConnRAII&&)                     = delete;
    SqlConnRAII& operator=(SqlConnRAII&&)          = delete;

private:
    MYSQL*      sql_;       // 当前持有的连接
    SqlConnPool* connPool_; // 所属连接池
};

#endif
