#ifndef USERSERVICE_H
#define USERSERVICE_H

#include <string>
#include <mysql/mysql.h>

// UserService：用户业务层（登录 / 注册）
// 这里只做业务相关接口，不放网络收发和 HTTP 解析逻辑
class UserService {
public:
    UserService() = delete;
    ~UserService() = delete;

    UserService(const UserService&) = delete;
    UserService& operator=(const UserService&) = delete;

    // 登录：用户名存在且密码匹配返回 true
    static bool Login(const std::string& username,
                      const std::string& password);

    // 注册：用户名不存在且写入成功返回 true
    static bool Register(const std::string& username,
                         const std::string& password);

private:
    // 查询用户密码（若用户存在则写入 password 并返回 true）
    static bool QueryUser_(MYSQL* sql,
                           const std::string& username,
                           std::string* password);

    // 新增用户（插入成功返回 true）
    static bool InsertUser_(MYSQL* sql,
                            const std::string& username,
                            const std::string& password);

    // SQL 转义，避免引号等特殊字符破坏 SQL 语句
    static std::string EscapeString_(MYSQL* sql,
                                     const std::string& str);
};

#endif
