#ifndef EPOLLER_H
#define EPOLLER_H

#include <cstddef>
#include <cstdint>
#include <vector>
#include <sys/epoll.h>

// Epoller：对 Linux epoll 的轻量封装
// 只负责 fd 事件注册、修改、删除和等待，不处理具体业务逻辑
class Epoller {
public:
    explicit Epoller(int maxEvent = 1024);
    ~Epoller();

    Epoller(const Epoller&) = delete;
    Epoller& operator=(const Epoller&) = delete;

    // 添加 fd 到 epoll，并设置关注的事件
    bool AddFd(int fd, uint32_t events);

    // 修改 fd 关注的事件
    bool ModFd(int fd, uint32_t events);

    // 从 epoll 中删除 fd
    bool DelFd(int fd);

    // 等待事件发生，返回就绪事件数量
    // timeoutMs = -1 表示无限等待
    int Wait(int timeoutMs = -1);

    // 获取第 i 个就绪事件对应的 fd
    int GetEventFd(size_t i) const;

    // 获取第 i 个就绪事件的事件类型
    uint32_t GetEvents(size_t i) const;

private:
    int epollFd_;                              // epoll 实例 fd
    std::vector<struct epoll_event> events_;   // epoll_wait 返回的就绪事件数组
};

#endif
