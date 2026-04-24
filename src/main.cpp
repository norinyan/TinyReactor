#include <iostream>
#include "buffer.h"
#include "log.h"
#include "threadpool.h"
#include "heaptimer.h"
#include "sqlconnpool.h"
#include "sqlconnRAII.h"
#include "httprequest.h"

#include <unistd.h>

int main() {
    // ---- 测 Buffer ----
    Buffer buff;
    buff.Append("hello world", 11);
    std::cout << "[Buffer] readable: " << buff.ReadableBytes() << std::endl;
    buff.Retrieve(6);
    std::cout << "[Buffer] after Retrieve(6): " 
              << std::string(buff.Peek(), buff.ReadableBytes()) << std::endl;

    // ---- 测 Log ----
    Log::Instance()->init(0, "/workspace/log", ".log", 1024);
    LOG_DEBUG("debug msg");
    LOG_INFO("info msg, port = %d", 1316);
    LOG_WARN("warn msg");
    LOG_ERROR("error msg");

    // ---- 测 ThreadPool ----
    ThreadPool pool(4);
    for (int i = 0; i < 8; ++i) {
        pool.AddTask([i] {
            std::cout << "task " << i << " running\n";
        });
    }
    sleep(1);

    // ---- 测 HeapTimer ----
    HeapTimer timer;

    // 添加三个连接，超时时间不同
    timer.add(1, 100, [] { std::cout << "[HeapTimer] fd=1 timeout, closed\n"; });
    timer.add(2, 200, [] { std::cout << "[HeapTimer] fd=2 timeout, closed\n"; });
    timer.add(3, 300, [] { std::cout << "[HeapTimer] fd=3 timeout, closed\n"; });

    std::cout << "[HeapTimer] next tick in: " << timer.getNextTick() << "ms\n";

    // 等 150ms，fd=1 应该超时，fd=2 fd=3 还没到
    usleep(150000);
    timer.tick();

    // 刷新 fd=2 的超时时间，再加 300ms
    timer.update(2, 300);

    // 等 200ms，fd=3 应该超时
    usleep(200000);
    timer.tick();

    // 等 200ms，fd=2 超时
    usleep(200000);
    timer.tick();

    // 2) 在 main() 里 return 0 前新增（建议放在 HeapTimer 测试后面）
    // ---- 测 SqlConnPool + SqlConnRAII ----
    // 在 dev 容器里连 mysql 服务：host="mysql", port=3306
    // 如果你在宿主机本地跑程序，则一般是 host="127.0.0.1", port=3307
    SqlConnPool* sqlpool = SqlConnPool::Instance();
    sqlpool->Init("mysql", 3306, "root", "root", "tinyreactor", 5);

    std::cout << "[SqlConnPool] free before: " << sqlpool->GetFreeConnCount() << std::endl;

    MYSQL* sql = nullptr;
    {
        // 进入作用域自动取连接，离开作用域自动还连接
        SqlConnRAII raii(&sql, sqlpool);

        if (!sql) {
            std::cout << "[SqlConnPool] GetConn failed\n";
        } else {
            // 最小可用性测试：SELECT 1
            if (mysql_query(sql, "SELECT 1;") == 0) {
                MYSQL_RES* res = mysql_store_result(sql);
                if (res) {
                    MYSQL_ROW row = mysql_fetch_row(res);
                    if (row && row[0]) {
                        std::cout << "[MySQL] SELECT 1 result: " << row[0] << std::endl;
                    }
                    mysql_free_result(res);
                } else {
                    // 理论上 SELECT 会有结果集，这里做个保护输出
                    std::cout << "[MySQL] store result empty, errno="
                            << mysql_errno(sql) << " msg=" << mysql_error(sql) << std::endl;
                }
            } else {
                std::cout << "[MySQL] query failed, errno="
                        << mysql_errno(sql) << " msg=" << mysql_error(sql) << std::endl;
            }
        }

        std::cout << "[SqlConnPool] free in scope: " << sqlpool->GetFreeConnCount() << std::endl;
    }
    std::cout << "[SqlConnPool] free after scope: " << sqlpool->GetFreeConnCount() << std::endl;

    sqlpool->ClosePool();

    // ---- 测 HttpRequest：GET ----
    {
        HttpRequest req;
        Buffer reqBuff;

        std::string rawGet =
            "GET /index HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Connection: keep-alive\r\n"
            "\r\n";

        reqBuff.Append(rawGet.c_str(), rawGet.size());

        bool ok = req.parse(reqBuff);
        std::cout << "[HttpRequest][GET] parse: " << (ok ? "ok" : "fail") << std::endl;
        std::cout << "[HttpRequest][GET] method: " << req.method() << std::endl;
        std::cout << "[HttpRequest][GET] path: " << req.path() << std::endl;       // 预期 /index.html
        std::cout << "[HttpRequest][GET] version: " << req.version() << std::endl; // 预期 1.1
        std::cout << "[HttpRequest][GET] keepAlive: " << req.IsKeepAlive() << std::endl;
    }

    // ---- 测 HttpRequest：POST（不走登录/注册，避免依赖数据库表）----
    {
        HttpRequest req;
        Buffer reqBuff;

        std::string body = "username=tom&password=123456&city=Bei+Jing";
        std::string rawPost =
            "POST /api HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Content-Type: application/x-www-form-urlencoded\r\n"
            "Content-Length: " + std::to_string(body.size()) + "\r\n"
            "\r\n" + body;

        reqBuff.Append(rawPost.c_str(), rawPost.size());

        bool ok = req.parse(reqBuff);
        std::cout << "[HttpRequest][POST] parse: " << (ok ? "ok" : "fail") << std::endl;
        std::cout << "[HttpRequest][POST] method: " << req.method() << std::endl;
        std::cout << "[HttpRequest][POST] path: " << req.path() << std::endl; // 预期 /api
        std::cout << "[HttpRequest][POST] username: " << req.GetPost("username") << std::endl;
        std::cout << "[HttpRequest][POST] password: " << req.GetPost("password") << std::endl;
        std::cout << "[HttpRequest][POST] city: " << req.GetPost("city") << std::endl; // 预期 "Bei Jing"
    }


    return 0;
}