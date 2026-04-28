#include "userservice.h"

#include "sqlconnRAII.h"

// ================================================================
// 1. Login：登录入口
//    用户名存在，并且数据库里的密码和用户输入密码一致，返回 true
// ================================================================
bool UserService::Login(const std::string& username,
                        const std::string& password) {
    if (username.empty() || password.empty()) {
        return false;
    }

    MYSQL* sql = nullptr;
    SqlConnRAII raii(&sql, SqlConnPool::Instance());

    if (!sql) {
        return false;
    }

    std::string dbPassword;
    if (!QueryUser_(sql, username, &dbPassword)) {
        return false;
    }

    return dbPassword == password;
}

// ================================================================
// 2. Register：注册入口
//    用户名不存在，并且插入数据库成功，返回 true
// ================================================================
bool UserService::Register(const std::string& username,
                           const std::string& password) {
    if (username.empty() || password.empty()) {
        return false;
    }

    MYSQL* sql = nullptr;
    SqlConnRAII raii(&sql, SqlConnPool::Instance());

    if (!sql) {
        return false;
    }

    std::string dbPassword;
    if (QueryUser_(sql, username, &dbPassword)) {
        return false;
    }

    return InsertUser_(sql, username, password);
}

// ================================================================
// 3. QueryUser_：查询用户密码
//    查到用户：把密码写入 password，返回 true
//    查不到用户：返回 false
// ================================================================
bool UserService::QueryUser_(MYSQL* sql,
                             const std::string& username,
                             std::string* password) {
    if (!sql || !password) {
        return false;
    }

    std::string safeUsername = EscapeString_(sql, username);

    std::string query =
        "SELECT password FROM user WHERE username='" +
        safeUsername +
        "' LIMIT 1";

    if (mysql_query(sql, query.c_str()) != 0) {
        return false;
    }

    MYSQL_RES* result = mysql_store_result(sql);
    if (!result) {
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    if (!row || !row[0]) {
        mysql_free_result(result);
        return false;
    }

    *password = row[0];

    mysql_free_result(result);
    return true;
}

// ================================================================
// 4. InsertUser_：新增用户
//    插入成功返回 true，插入失败返回 false
// ================================================================
bool UserService::InsertUser_(MYSQL* sql,
                              const std::string& username,
                              const std::string& password) {
    if (!sql) {
        return false;
    }

    std::string safeUsername = EscapeString_(sql, username);
    std::string safePassword = EscapeString_(sql, password);

    std::string query =
        "INSERT INTO user(username, password) VALUES('" +
        safeUsername +
        "', '" +
        safePassword +
        "')";

    return mysql_query(sql, query.c_str()) == 0;
}

// ================================================================
// 5. EscapeString_：SQL 字符串转义
//    防止用户名 / 密码里的引号等特殊字符破坏 SQL 语句
// ================================================================
std::string UserService::EscapeString_(MYSQL* sql,
                                       const std::string& str) {
    if (!sql || str.empty()) {
        return "";
    }

    std::string result;
    result.resize(str.size() * 2 + 1);

    unsigned long len = mysql_real_escape_string(sql,
                                                 &result[0],
                                                 str.c_str(),
                                                 str.size());

    result.resize(len);
    return result;
}
