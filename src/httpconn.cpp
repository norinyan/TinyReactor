#include "httpconn.h"
#include "userservice.h"

#include <arpa/inet.h>
#include <cerrno>
#include <unistd.h>

// ================================================================
// 1. 静态成员 / 构造析构 / 连接生命周期
//    HttpConn 表示一个客户端连接，保存 fd、地址、读写缓冲区和 HTTP 上下文
// ================================================================
bool HttpConn::isET = true;
const char* HttpConn::srcDir = "./resources";
std::atomic<int> HttpConn::userCount{0};

HttpConn::HttpConn()
    : fd_(-1),
      addr_{},
      isClose_(true),
      readBuff_(),
      writeBuff_(),
      request_(),
      response_(),
      iov_{},
      iovCnt_(0) {}

HttpConn::~HttpConn() {
    Close();
}

void HttpConn::Init(int fd, const sockaddr_in& addr) {
    fd_ = fd;
    addr_ = addr;
    isClose_ = false;

    readBuff_.RetrieveAll();
    writeBuff_.RetrieveAll();
    request_.Init();
    response_.UnmapFile();

    iov_[0].iov_base = nullptr;
    iov_[0].iov_len = 0;
    iov_[1].iov_base = nullptr;
    iov_[1].iov_len = 0;
    iovCnt_ = 0;

    ++userCount;
}

void HttpConn::Close() {
    if (!isClose_) {
        isClose_ = true;
        response_.UnmapFile();

        if (fd_ >= 0) {
            close(fd_);
            fd_ = -1;
        }

        --userCount;
    }
}

void HttpConn::Reset() {
    readBuff_.RetrieveAll();
    writeBuff_.RetrieveAll();

    request_.Init();
    response_.UnmapFile();

    iov_[0].iov_base = nullptr;
    iov_[0].iov_len = 0;
    iov_[1].iov_base = nullptr;
    iov_[1].iov_len = 0;
    iovCnt_ = 0;
}

// ================================================================
// 2. Socket 读写
//    Read：把 socket 数据读进 readBuff_
//    Write：把 writeBuff_ + mmap 文件内容通过 writev 写回 socket
// ================================================================
ssize_t HttpConn::Read(int* saveErrno) {
    ssize_t totalLen = 0;

    if (fd_ < 0) {
        return -1;
    }

    do {
        ssize_t len = readBuff_.ReadFd(fd_, saveErrno);

        if (len <= 0) {
            if (len < 0 && (*saveErrno == EAGAIN || *saveErrno == EWOULDBLOCK)) {
                break;
            }
            return len;
        }

        totalLen += len;
    } while (isET);

    return totalLen;
}

ssize_t HttpConn::Write(int* saveErrno) {
    ssize_t totalLen = 0;

    if (fd_ < 0) {
        return -1;
    }

    while (ToWriteBytes() > 0) {
        ssize_t len = writev(fd_, iov_, iovCnt_);

        if (len <= 0) {
            *saveErrno = errno;
            return len;
        }

        totalLen += len;
        size_t writeLen = static_cast<size_t>(len);

        if (writeLen <= iov_[0].iov_len) {
            writeBuff_.Retrieve(writeLen);
            iov_[0].iov_base = const_cast<char*>(writeBuff_.Peek());
            iov_[0].iov_len = writeBuff_.ReadableBytes();
        } else {
            size_t headerLen = iov_[0].iov_len;

            if (headerLen > 0) {
                writeBuff_.Retrieve(headerLen);
                iov_[0].iov_base = const_cast<char*>(writeBuff_.Peek());
                iov_[0].iov_len = 0;
            }

            size_t fileLen = writeLen - headerLen;
            iov_[1].iov_base = static_cast<char*>(iov_[1].iov_base) + fileLen;
            iov_[1].iov_len -= fileLen;
        }
    }

    response_.UnmapFile();
    writeBuff_.RetrieveAll();

    iov_[0].iov_base = nullptr;
    iov_[0].iov_len = 0;
    iov_[1].iov_base = nullptr;
    iov_[1].iov_len = 0;
    iovCnt_ = 0;

    return totalLen;
}

// ================================================================
// 3. 一次 HTTP 请求处理流程
//    解析请求 -> 简单路由 -> 生成响应 -> 设置 writev 的 iovec
// ================================================================
bool HttpConn::Process() {
    if (readBuff_.ReadableBytes() <= 0) {
        return false;
    }

    writeBuff_.RetrieveAll();
    response_.UnmapFile();

    HttpRequest::PARSE_RESULT parseRet = request_.Parse(readBuff_);

    if (parseRet == HttpRequest::NO_REQUEST) {
        return false;
    }

    int code = -1;
    std::string path;

    if (parseRet == HttpRequest::BAD_REQUEST) {
        code = 400;
        path = "/400.html";
    } else {
        code = 200;
        path = HandleRequest_();
    }

    response_.Init(srcDir, path, IsKeepAlive(), code);
    response_.MakeResponse(writeBuff_);

    iov_[0].iov_base = const_cast<char*>(writeBuff_.Peek());
    iov_[0].iov_len = writeBuff_.ReadableBytes();
    iovCnt_ = 1;

    if (response_.File() && response_.FileLen() > 0) {
        iov_[1].iov_base = response_.File();
        iov_[1].iov_len = response_.FileLen();
        iovCnt_ = 2;
    } else {
        iov_[1].iov_base = nullptr;
        iov_[1].iov_len = 0;
    }

    return true;
}

// ================================================================
// 4. 查询接口 / 简单路由
//    第一阶段只做静态 GET：请求什么路径，就交给 HttpResponse 找对应文件
// ================================================================
int HttpConn::GetFd() const {
    return fd_;
}

int HttpConn::GetPort() const {
    return ntohs(addr_.sin_port);
}

const char* HttpConn::GetIP() const {
    return inet_ntoa(addr_.sin_addr);
}

sockaddr_in HttpConn::GetAddr() const {
    return addr_;
}

bool HttpConn::IsKeepAlive() const {
    return request_.IsKeepAlive();
}

size_t HttpConn::ToWriteBytes() const {
    return iov_[0].iov_len + iov_[1].iov_len;
}

std::string HttpConn::HandleRequest_() {
    if (request_.Method() == "GET") {
        return request_.Path();
    }

    if (request_.Method() == "POST") {
        std::string username = request_.GetPost("username");
        std::string password = request_.GetPost("password");

        if (request_.Path() == "/login.html") {
            if (UserService::Login(username, password)) {
                return "/welcome.html";
            }
            return "/error.html";
        }


        if (request_.Path() == "/register.html") {
            if (UserService::Register(username, password)) {
                return "/welcome.html";
            }
            return "/error.html";
        }

    }

    return "/405.html";
}
