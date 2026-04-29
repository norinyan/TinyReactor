#ifndef HTTPCONN_H
#define HTTPCONN_H

#include <atomic>
#include <string>
#include <sys/types.h>
#include <sys/uio.h>
#include <netinet/in.h>

#include "buffer.h"
#include "httprequest.h"
#include "httpresponse.h"

class HttpConn {
public:
    HttpConn();
    ~HttpConn();

    // 初始化一个客户端连接
    void Init(int fd, const sockaddr_in& addr);

    // 关闭连接
    void Close();

    // 重置一次请求/响应上下文，保留当前连接 fd
    void Reset();
    
    // 从 socket 读数据到 readBuff_
    ssize_t Read(int* saveErrno);

    // 把 writeBuff_ 和文件映射区写回 socket
    ssize_t Write(int* saveErrno);

    // 一次请求处理流程：解析请求 -> 路由 -> 生成响应
    bool Process();

    int GetFd() const;
    int GetPort() const;
    const char* GetIP() const;
    sockaddr_in GetAddr() const;

    // 是否保持长连接
    bool IsKeepAlive() const;

    // 当前还剩多少字节待发送
    size_t ToWriteBytes() const;

private:
    // 第一阶段只做静态 GET 路由（例如 / -> /index.html）
    std::string HandleRequest_();

private:
    int fd_;                    // 客户端 socket fd
    sockaddr_in addr_;          // 客户端地址
    bool isClose_;              // 是否已关闭连接

    Buffer readBuff_;           // 读缓冲区
    Buffer writeBuff_;          // 写缓冲区

    HttpRequest request_;       // 请求解析器
    HttpResponse response_;     // 响应生成器

    struct iovec iov_[2];       // writev 分散写
    int iovCnt_;                // iovec 使用数量

public:
    static bool isET;                   // 是否 ET 模式
    static const char* srcDir;          // 静态资源根目录
    static std::atomic<int> userCount;  // 当前在线连接数
};

#endif
