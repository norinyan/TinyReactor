#include "epoller.h"

#include <cassert>
#include <cerrno>
#include <unistd.h>

// ================================================================
// 1. 构造 / 析构
//    构造函数创建 epoll 实例，并准备好接收就绪事件的数组
//    析构函数关闭 epollFd_，释放内核资源
// ================================================================
Epoller::Epoller(int maxEvent)
    : epollFd_(epoll_create1(0)),
      events_(maxEvent > 0 ? maxEvent : 1024) {
    // epoll_create1 失败时返回 -1，这里用 assert 在开发阶段及时暴露问题
    assert(epollFd_ >= 0);
}

Epoller::~Epoller() {
    if (epollFd_ >= 0) {
        close(epollFd_);
        epollFd_ = -1;
    }
}

// ================================================================
// 2. AddFd：添加 fd 到 epoll
//    fd：要监听的对象，例如 listenFd 或 clientFd
//    events：要关注的事件，例如 EPOLLIN / EPOLLOUT / EPOLLET
// ================================================================
bool Epoller::AddFd(int fd, uint32_t events) {
    if (fd < 0 || epollFd_ < 0) {
        return false;
    }

    struct epoll_event event = {};
    event.data.fd = fd;       // epoll_wait 返回时，通过这个字段知道是哪个 fd 就绪
    event.events = events;    // 告诉 epoll 关注哪些事件

    return epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &event) == 0;
}

// ================================================================
// 3. ModFd：修改 fd 关注的事件
//    常见场景：
//    - 请求读完并生成响应后，把 clientFd 从 EPOLLIN 改成 EPOLLOUT
//    - 响应写完后，如果 keep-alive，再从 EPOLLOUT 改回 EPOLLIN
// ================================================================
bool Epoller::ModFd(int fd, uint32_t events) {
    if (fd < 0 || epollFd_ < 0) {
        return false;
    }

    struct epoll_event event = {};
    event.data.fd = fd;
    event.events = events;

    return epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &event) == 0;
}

// ================================================================
// 4. DelFd：从 epoll 中删除 fd
//    常见场景：连接关闭时，先从 epoll 移除，再关闭 socket
// ================================================================
bool Epoller::DelFd(int fd) {
    if (fd < 0 || epollFd_ < 0) {
        return false;
    }

    // EPOLL_CTL_DEL 不需要 event 参数，传 nullptr 即可
    return epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr) == 0;
}

// ================================================================
// 5. Wait：等待事件发生
//    timeoutMs：
//      -1：一直等，直到有事件发生
//       0：不阻塞，立刻返回
//      >0：最多等待 timeoutMs 毫秒
//
//    返回值：
//      >0：就绪事件数量
//       0：超时，当前没有事件
//      -1：出错
// ================================================================
int Epoller::Wait(int timeoutMs) {
    if (epollFd_ < 0 || events_.empty()) {
        return -1;
    }

    int ret = epoll_wait(
        epollFd_,
        events_.data(),
        static_cast<int>(events_.size()),
        timeoutMs
    );

    // epoll_wait 可能被信号打断，此时 errno == EINTR
    // 对事件循环来说，这不算真正错误，返回 0 表示本轮没有事件即可
    if (ret < 0 && errno == EINTR) {
        return 0;
    }

    return ret;
}

// ================================================================
// 6. GetEventFd：获取第 i 个就绪事件对应的 fd
//    注意：调用方应保证 i < Wait() 的返回值
// ================================================================
int Epoller::GetEventFd(size_t i) const {
    assert(i < events_.size());
    return events_[i].data.fd;
}

// ================================================================
// 7. GetEvents：获取第 i 个就绪事件的事件类型
//    注意：调用方应保证 i < Wait() 的返回值
// ================================================================
uint32_t Epoller::GetEvents(size_t i) const {
    assert(i < events_.size());
    return events_[i].events;
}
