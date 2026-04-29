#include "log.h"
#include "sqlconnpool.h"
#include "webserver.h"

int main() {
    // 日志：level=0 输出 debug 及以上；1024 表示开启异步日志队列
    Log::Instance()->init(0, "./log", ".log", 1024);

    // 初始化 MySQL 连接池
    SqlConnPool::Instance()->Init("mysql",
                                  3306,
                                  "root",
                                  "root",
                                  "tinyreactor",
                                  5);

    // 启动 WebServer
    // 参数：端口、线程数、连接超时毫秒、静态资源目录、是否 ET 模式
    WebServer server(1316, 8, 60000, "../resources", true);
    server.Start();

    return 0;
}
