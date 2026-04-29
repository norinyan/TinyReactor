#include "webserver.h"
#include "log.h"
#include "sqlconnpool.h"

#include <cassert>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>


// ================================================================
// 1. 构造 / 析构
//    WebServer 持有整个服务器运行所需的核心模块：
//    ThreadPool：工作线程池
//    Epoller：I/O 事件通知
//    HeapTimer：连接超时管理
//    users_：fd 到 HttpConn 的映射数组
// ================================================================
WebServer::WebServer(int port,
                     int threadNum,
                     int timeoutMs,
                     const char* srcDir,
                     bool isET)
    : port_(port),
      timeoutMs_(timeoutMs),
      listenFd_(-1),
      isClose_(false),
      isET_(isET),
      srcDir_(srcDir),
      listenEvent_(0),
      connEvent_(0),
      threadPool_(std::make_unique<ThreadPool>(threadNum)),
      epoller_(std::make_unique<Epoller>()),
      timer_(),
      users_(MAX_FD) {
    assert(port_ > 0);
    assert(threadNum > 0);
    assert(timeoutMs_ >= 0);

    HttpConn::srcDir = srcDir_.c_str();
    HttpConn::isET = isET_;

    InitEventMode_();
}

WebServer::~WebServer() {
    if (listenFd_ >= 0) {
        close(listenFd_);
        listenFd_ = -1;
    }

    isClose_ = true;

    SqlConnPool::Instance()->ClosePool();
}

// ================================================================
// 2. InitEventMode_：初始化 epoll 事件模式
//    listenEvent_：监听 socket 的事件
//    connEvent_：客户端 socket 的事件
//
//    EPOLLIN：可读事件
//    EPOLLRDHUP：对端关闭连接时通知
//    EPOLLONESHOT：同一时刻只让一个线程处理这个 fd，避免并发读写同一连接
//    EPOLLET：边沿触发模式，需要配合非阻塞 fd
// ================================================================
void WebServer::InitEventMode_() {
    listenEvent_ = EPOLLIN;
    connEvent_ = EPOLLIN | EPOLLRDHUP | EPOLLONESHOT;

    if (isET_) {
        listenEvent_ |= EPOLLET;
        connEvent_ |= EPOLLET;
    }
}

// ================================================================
// 3. SetFdNonblock_：把 fd 设置成非阻塞
//    返回旧的 fd flag，失败返回 -1
// ================================================================
int WebServer::SetFdNonblock_(int fd) {
    assert(fd >= 0);

    int oldFlag = fcntl(fd, F_GETFL, 0);
    if (oldFlag < 0) {
        return -1;
    }

    int newFlag = oldFlag | O_NONBLOCK;
    if (fcntl(fd, F_SETFL, newFlag) < 0) {
        return -1;
    }

    return oldFlag;
}

// ================================================================
// 4. 下面这些函数先放空壳
//    目的：先让 webserver.cpp 能和 webserver.h 对齐并编译通过
//    后续会按阶段逐个补实现
// ================================================================
void WebServer::Start() {
    if (!InitSocket_()) {
        isClose_ = true;
        LOG_ERROR("server init socket failed");
        return;
    }

    LOG_INFO("server start, port=%d", port_);

    while (!isClose_) {
        int timeout = timer_.getNextTick();
        int eventCnt = epoller_->Wait(timeout);

        if (eventCnt < 0) {
            LOG_ERROR("epoller wait failed");
            break;
        }

        for (int i = 0; i < eventCnt; ++i) {
            int fd = epoller_->GetEventFd(static_cast<size_t>(i));
            uint32_t events = epoller_->GetEvents(static_cast<size_t>(i));

            if (fd == listenFd_) {
            HandleListen_();
            } else if ((events & EPOLLHUP) && !(events & EPOLLIN)) {
                CloseConn_(&users_[fd]);
            } else if (events & EPOLLERR) {
                CloseConn_(&users_[fd]);
            } else if (events & EPOLLIN) {
                HandleRead_(&users_[fd]);
            } else if (events & EPOLLOUT) {
                HandleWrite_(&users_[fd]);
            } else if (events & EPOLLRDHUP) {
                CloseConn_(&users_[fd]);
            } else {
                LOG_INFO("unexpected client event, fd=%d, events=%u", fd, events);
            }


        }
    }
}

bool WebServer::InitSocket_() {
    int ret = 0;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        LOG_ERROR("create listen socket failed, port=%d", port_);
        return false;
    }

    // 端口复用：服务器异常退出后，能更快重新绑定同一个端口
    int optVal = 1;
    ret = setsockopt(listenFd_,
                     SOL_SOCKET,
                     SO_REUSEADDR,
                     &optVal,
                     sizeof(optVal));
    if (ret < 0) {
        LOG_ERROR("setsockopt SO_REUSEADDR failed, port=%d", port_);
        close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    ret = bind(listenFd_,
               reinterpret_cast<sockaddr*>(&addr),
               sizeof(addr));
    if (ret < 0) {
        LOG_ERROR("bind failed, port=%d", port_);
        close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    ret = listen(listenFd_, LISTEN_BACKLOG);
    if (ret < 0) {
        LOG_ERROR("listen failed, port=%d", port_);
        close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    ret = SetFdNonblock_(listenFd_);
    if (ret < 0) {
        LOG_ERROR("set listen fd nonblock failed, port=%d", port_);
        close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    if (!epoller_->AddFd(listenFd_, listenEvent_)) {
        LOG_ERROR("add listen fd to epoller failed, port=%d", port_);
        close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    LOG_INFO("server listen fd init success, port=%d", port_);

    return true;
}


void WebServer::AddClient_(int fd, sockaddr_in addr) {
    assert(fd >= 0);
    assert(fd < MAX_FD);

    users_[fd].Init(fd, addr);

    if (timeoutMs_ > 0) {
        timer_.add(fd, timeoutMs_, [this, fd]() {
            CloseConn_(&users_[fd]);
        });
    }

    if (SetFdNonblock_(fd) < 0) {
        LOG_ERROR("set client fd nonblock failed, fd=%d", fd);
        CloseConn_(&users_[fd]);
        return;
    }

    if (!epoller_->AddFd(fd, connEvent_)) {
        LOG_ERROR("add client fd to epoller failed, fd=%d", fd);
        CloseConn_(&users_[fd]);
        return;
    }

    LOG_INFO("client connected, fd=%d, ip=%s, port=%d",
             fd,
             users_[fd].GetIP(),
             users_[fd].GetPort());
}


void WebServer::CloseConn_(HttpConn* client) {
    assert(client);

    int fd = client->GetFd();
    if (fd < 0) {
        return;
    }

    if (timeoutMs_ > 0) {
        timer_.remove(fd);   
    }

    epoller_->DelFd(fd);
    client->Close();

    LOG_INFO("client closed, fd=%d", fd);
}


void WebServer::ExtendTime_(HttpConn* client) {
    assert(client);

    if (timeoutMs_ > 0) {
        timer_.update(client->GetFd(), timeoutMs_);
    }
}


void WebServer::HandleListen_() {
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);

    do {
        int fd = accept(listenFd_,
                        reinterpret_cast<sockaddr*>(&addr),
                        &len);

        if (fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }

            LOG_ERROR("accept client failed, errno=%d", errno);
            break;
        }

        if (fd >= MAX_FD) {
            LOG_WARN("client fd too large, fd=%d", fd);
            close(fd);
            continue;
        }

        AddClient_(fd, addr);
    } while (isET_);
}


void WebServer::HandleRead_(HttpConn* client) {
    assert(client);

    ExtendTime_(client);

    threadPool_->AddTask([this, client]() {
        OnRead_(client);
    });
}


void WebServer::HandleWrite_(HttpConn* client) {
    assert(client);

    ExtendTime_(client);

    threadPool_->AddTask([this, client]() {
        OnWrite_(client);
    });
}


void WebServer::OnRead_(HttpConn* client) {
    assert(client);

    int saveErrno = 0;
    ssize_t readLen = client->Read(&saveErrno);

    if (readLen <= 0 && saveErrno != EAGAIN) {
        LOG_INFO("client read failed or closed, fd=%d, errno=%d",
                 client->GetFd(),
                 saveErrno);
        CloseConn_(client);
        return;
    }

    LOG_INFO("client read success, fd=%d, bytes=%ld",
             client->GetFd(),
             static_cast<long>(readLen));

    OnProcess_(client);
}


void WebServer::OnWrite_(HttpConn* client) {
    assert(client);

    int saveErrno = 0;
    ssize_t writeLen = client->Write(&saveErrno);

    if (client->ToWriteBytes() == 0) {
        if (client->IsKeepAlive()) {
            LOG_INFO("client response sent, keep alive, fd=%d, bytes=%ld",
                     client->GetFd(),
                     static_cast<long>(writeLen));

            client->Reset();
            epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLIN);
            return;
        }

        LOG_INFO("client response sent, close connection, fd=%d, bytes=%ld",
                 client->GetFd(),
                 static_cast<long>(writeLen));
        CloseConn_(client);
        return;
    }

    if (writeLen < 0) {
        if (saveErrno == EAGAIN) {
            epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
            LOG_INFO("client write would block, continue EPOLLOUT, fd=%d",
                     client->GetFd());
            return;
        }

        LOG_INFO("client write failed, fd=%d, errno=%d",
                 client->GetFd(),
                 saveErrno);
        CloseConn_(client);
        return;
    }

    epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
}


void WebServer::OnProcess_(HttpConn* client) {
    assert(client);

    if (client->Process()) {
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
        LOG_INFO("client process success, switch to EPOLLOUT, fd=%d",
                 client->GetFd());
    } else {
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLIN);
        LOG_INFO("client request incomplete, switch to EPOLLIN, fd=%d",
                 client->GetFd());
    }
}

