#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <netinet/in.h>

#include "epoller.h"
#include "heaptimer.h"
#include "httpconn.h"
#include "threadpool.h"

// WebServer：Reactor 总调度中心
// 负责监听端口、接收连接、等待 epoll 事件，并把事件分发给 HttpConn 处理
class WebServer {
public:
    explicit WebServer(int port = 1316,
                       int threadNum = 8,
                       int timeoutMs = 60000,
                       const char* srcDir = "../resources",
                       bool isET = true);

    ~WebServer();

    WebServer(const WebServer&) = delete;
    WebServer& operator=(const WebServer&) = delete;

    // 启动服务器事件循环
    void Start();

private:
    // 初始化监听 socket：socket / bind / listen / 加入 epoll
    bool InitSocket_();

    // 初始化 listenFd 和 clientFd 的事件模式
    void InitEventMode_();

    // 设置 fd 为非阻塞
    static int SetFdNonblock_(int fd);

    // 新客户端接入：初始化 HttpConn、加入 epoll、加入定时器
    void AddClient_(int fd, sockaddr_in addr);

    // 关闭连接：从 epoll 删除、关闭 HttpConn
    void CloseConn_(HttpConn* client);

    // 刷新连接超时时间
    void ExtendTime_(HttpConn* client);

    // 处理监听 fd 可读：accept 新连接
    void HandleListen_();

    // 处理客户端可读事件：投递读任务到线程池
    void HandleRead_(HttpConn* client);

    // 处理客户端可写事件：投递写任务到线程池
    void HandleWrite_(HttpConn* client);

    // 工作线程中执行实际读取
    void OnRead_(HttpConn* client);

    // 工作线程中执行实际写入
    void OnWrite_(HttpConn* client);

    // 请求处理完成后，根据结果切换 EPOLLIN / EPOLLOUT
    void OnProcess_(HttpConn* client);

private:
    static constexpr int MAX_FD = 65536;       // 支持的最大 fd 数量
    static constexpr int LISTEN_BACKLOG = 6;   // listen 等待队列长度

    int port_;             // 监听端口
    int timeoutMs_;        // 连接超时时间，毫秒
    int listenFd_;         // 监听 socket fd
    bool isClose_;         // 服务器是否关闭
    bool isET_;            // 是否使用 ET 模式

    std::string srcDir_;   // 静态资源目录

    uint32_t listenEvent_; // 监听 fd 的 epoll 事件
    uint32_t connEvent_;   // 客户端 fd 的 epoll 事件

    std::unique_ptr<ThreadPool> threadPool_; // 工作线程池
    std::unique_ptr<Epoller> epoller_;       // epoll 封装

    HeapTimer timer_;              // 连接超时管理
    std::vector<HttpConn> users_;  // fd -> HttpConn，按 fd 下标直接访问
};

#endif
