阶段1：接口冻结（先搭骨架）
先写 include/epoller.h，只定成员和函数签名；再写 src/epoller.cpp 空实现，保证能编译通过。
理由：Epoller 是 WebServer 的底层事件封装，先把外部调用方式定下来，后面 WebServer 接入时不用反复改接口。

阶段2：epoll 基础封装
实现 epoll_create1、epoll_ctl 的 ADD / MOD / DEL，以及 epoll_wait。
理由：先把最核心的 fd 事件监听跑通，保证能够注册 fd、等待事件、取出就绪 fd。

阶段3：socketpair 最小测试
用 socketpair 创建一对 fd，把其中一个 fd 加入 Epoller；往另一个 fd 写数据；调用 Wait；验证能拿到可读事件。
理由：不依赖真实网络端口，能快速确认 Epoller 本身可用。

阶段4：和 HttpConn 的接入准备
确认 Epoller 暴露的 GetEventFd / GetEvents 能满足 WebServer 调度 HttpConn::Read / Process / Write。
理由：Epoller 不处理 HTTP，只负责告诉 WebServer 哪些 fd 就绪。

阶段5：稳定性收尾
处理 epoll_wait 被信号打断、非法 fd、事件数组大小、ET/LT 模式标志组合等细节。
理由：基础功能跑通后再增强边界处理，避免一开始过度设计。


Epoller 模块设计大纲

目录结构：

include/
└── epoller.h

src/
└── epoller.cpp


============================================================
1. Epoller：I/O 事件通知封装
============================================================

文件：include/epoller.h

需要包含：

#include <cstddef>
#include <cstdint>
#include <vector>
#include <sys/epoll.h>

class Epoller {
public:
    explicit Epoller(int maxEvent = 1024);
    ~Epoller();

    bool AddFd(int fd, uint32_t events);
    bool ModFd(int fd, uint32_t events);
    bool DelFd(int fd);

    int Wait(int timeoutMs = -1);

    int GetEventFd(size_t i) const;
    uint32_t GetEvents(size_t i) const;

private:
    int epollFd_;
    std::vector<struct epoll_event> events_;
};


文件：src/epoller.cpp

Epoller::Epoller(int maxEvent);
Epoller::~Epoller();

bool Epoller::AddFd(int fd, uint32_t events);
bool Epoller::ModFd(int fd, uint32_t events);
bool Epoller::DelFd(int fd);

int Epoller::Wait(int timeoutMs);

int Epoller::GetEventFd(size_t i) const;
uint32_t Epoller::GetEvents(size_t i) const;


============================================================
2. 核心职责
============================================================

Epoller 只负责封装 Linux epoll，不负责业务逻辑。

它应该做：

1. 创建 epoll 实例
   epoll_create1(0)

2. 注册 fd 事件
   epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &event)

3. 修改 fd 事件
   epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &event)

4. 删除 fd
   epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr)

5. 等待事件
   epoll_wait(epollFd_, events_.data(), events_.size(), timeoutMs)

6. 提供就绪事件查询
   GetEventFd(i)
   GetEvents(i)


============================================================
3. 不属于 Epoller 的职责
============================================================

Epoller 不应该做：

1. 不 accept 新连接
   accept 属于 WebServer。

2. 不读写 socket 数据
   Read / Write 属于 HttpConn。

3. 不解析 HTTP
   HTTP 解析属于 HttpRequest。

4. 不生成响应
   响应生成属于 HttpResponse。

5. 不管理连接超时
   超时管理属于 HeapTimer。

6. 不分发线程任务
   任务分发属于 ThreadPool / WebServer。


============================================================
4. 事件流
============================================================

客户端 socket
    |
    | fd 注册到 epoll
    v
+----------------+
|    Epoller     |
+----------------+
| epollFd_       |
| events_        |
+----------------+
    |
    | Wait(timeout)
    v
+----------------+
| 就绪事件数组    |
+----------------+
    |
    | GetEventFd(i)
    | GetEvents(i)
    v
+----------------+
|   WebServer    |
+----------------+
| 判断事件类型    |
| 调 HttpConn     |
+----------------+
    |
    | EPOLLIN  -> HttpConn::Read / Process
    | EPOLLOUT -> HttpConn::Write
    v
+----------------+
|   HttpConn     |
+----------------+


============================================================
5. 和 WebServer 的关系
============================================================

WebServer 后面会持有一个 Epoller：

Epoller epoller_;

监听 socket 初始化后：

epoller_.AddFd(listenFd_, listenEvent);

客户端连接进来后：

epoller_.AddFd(clientFd, connEvent);

事件循环里：

int eventCnt = epoller_.Wait(timeoutMs);

for (int i = 0; i < eventCnt; ++i) {
    int fd = epoller_.GetEventFd(i);
    uint32_t events = epoller_.GetEvents(i);

    if (fd == listenFd_) {
        // accept 新连接
    } else if (events & EPOLLIN) {
        // 读请求
    } else if (events & EPOLLOUT) {
        // 写响应
    }
}


============================================================
6. socketpair 单测计划
============================================================

测试目标：

不启动真实服务器，也能验证 Epoller 是否能监听 fd 可读事件。

测试流程：

1. socketpair(AF_UNIX, SOCK_STREAM, 0, fds)

2. Epoller epoller;

3. epoller.AddFd(fds[1], EPOLLIN)

4. write(fds[0], "hello", 5)

5. int n = epoller.Wait(1000)

6. 断言 n > 0

7. epoller.GetEventFd(0) 应该等于 fds[1]

8. epoller.GetEvents(0) 应该包含 EPOLLIN


============================================================
7. 第一版实现原则
============================================================

1. 只封装必要接口
   AddFd / ModFd / DelFd / Wait / GetEventFd / GetEvents。

2. 不引入复杂抽象
   Epoller 是系统调用薄封装，不需要模板和回调。

3. 失败返回 false 或 -1
   具体日志后面接 WebServer 时再统一补。

4. 默认事件数组大小 1024
   够当前学习和测试使用，后面可配置。

5. 保持资源生命周期清晰
   构造时创建 epoll fd，析构时 close。


============================================================
8. 完成标准
============================================================

1. include/epoller.h 和 src/epoller.cpp 能编译通过。

2. AddFd / ModFd / DelFd 返回值正确。

3. Wait 能返回就绪事件数量。

4. GetEventFd / GetEvents 能取出 epoll_wait 填充的事件。

5. socketpair 测试能验证 EPOLLIN 事件。

6. CMakeLists.txt 已加入 src/epoller.cpp。


核心边界：

Epoller
    只封装 epoll
    不处理 HTTP
    不处理连接业务
    不管理定时器

WebServer
    负责事件循环
    负责 accept
    负责调度 HttpConn
    负责配合 ThreadPool / HeapTimer

HttpConn
    负责单个连接的读写和 HTTP 请求响应流程


============================================================
9. 第一版细节约定
============================================================

1. include/epoller.h 需要包含：

   <cstddef>
   <cstdint>
   <vector>
   <sys/epoll.h>

   原因：
   size_t 来自 <cstddef>；
   uint32_t 来自 <cstdint>；
   std::vector 来自 <vector>；
   epoll_event / EPOLLIN 等来自 <sys/epoll.h>。

2. 构造函数中使用 epoll_create1(0) 创建 epollFd_。

   第一版可以使用 assert(epollFd_ >= 0) 做开发期保护。
   后面 WebServer 联调时，如果需要更完整错误处理，再统一增强。

3. AddFd / ModFd / DelFd 遇到非法 fd 时直接返回 false。

   例如：
   fd < 0
   epollFd_ < 0

   Epoller 不负责修复非法 fd，只负责拒绝操作。

4. GetEventFd / GetEvents 使用 assert(i < events_.size()) 做开发期保护。

   调用方应该只在 0 <= i < Wait() 返回值 的范围内取事件。

5. Wait 第一版直接返回 epoll_wait 的返回值。

   返回值含义：
   > 0：就绪事件数量
   = 0：超时
   < 0：出错

   EINTR 等边界处理可以放到 WebServer 联调阶段再收尾。

6. 第一版不做日志输出。

   Epoller 是底层系统调用薄封装，失败时返回 false 或 -1。
   是否记录日志由后面的 WebServer 决定。



main.cpp测试代码：

#include "epoller.h"

#include <iostream>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

int main() {
    int fds[2] = {-1, -1};

    // socketpair 创建一对互相连接的 socket fd
    // fds[0] 写入数据后，fds[1] 会变成可读
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) {
        std::cerr << "socketpair failed" << std::endl;
        return 1;
    }

    Epoller epoller;

    // 监听 fds[1] 的可读事件
    if (!epoller.AddFd(fds[1], EPOLLIN)) {
        std::cerr << "AddFd failed" << std::endl;
        close(fds[0]);
        close(fds[1]);
        return 1;
    }

    std::string msg = "hello epoller";

    // 往 fds[0] 写数据，触发 fds[1] 的 EPOLLIN
    ssize_t writeLen = write(fds[0], msg.c_str(), msg.size());
    if (writeLen < 0) {
        std::cerr << "write failed" << std::endl;
        close(fds[0]);
        close(fds[1]);
        return 1;
    }

    // 最多等待 1000ms，正常情况下会立刻返回 1 个事件
    int eventCnt = epoller.Wait(1000);
    if (eventCnt < 0) {
        std::cerr << "Epoller Wait failed" << std::endl;
        close(fds[0]);
        close(fds[1]);
        return 1;
    }

    if (eventCnt == 0) {
        std::cerr << "Epoller Wait timeout" << std::endl;
        close(fds[0]);
        close(fds[1]);
        return 1;
    }

    std::cout << "event count: " << eventCnt << std::endl;

    bool ok = false;

    for (int i = 0; i < eventCnt; ++i) {
        int fd = epoller.GetEventFd(static_cast<size_t>(i));
        uint32_t events = epoller.GetEvents(static_cast<size_t>(i));

        std::cout << "event fd: " << fd << std::endl;
        std::cout << "target fd: " << fds[1] << std::endl;
        std::cout << "events: " << events << std::endl;

        if (fd == fds[1] && (events & EPOLLIN)) {
            ok = true;
        }
    }

    if (ok) {
        std::cout << "Epoller socketpair test passed" << std::endl;
    } else {
        std::cerr << "Epoller socketpair test failed" << std::endl;
    }

    close(fds[0]);
    close(fds[1]);

    return ok ? 0 : 1;
}
