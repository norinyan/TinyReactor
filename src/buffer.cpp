#include "buffer.h"

#include <algorithm>
#include <cerrno>
#include <sys/uio.h>
#include <unistd.h>

// 构造函数：初始化底层缓冲区大小，并把读写位置都设为 0
Buffer::Buffer(size_t initBuffSize)
    : buffer_(initBuffSize), readPos_(0), writePos_(0) {}

  // 当前可读数据长度 = 写位置 - 读位置
size_t Buffer::ReadableBytes() const {
    return writePos_ - readPos_;
}

 // 当前可写空间长度 = 缓冲区总大小 - 写位置
size_t Buffer::WritableBytes() const {
    return buffer_.size() - writePos_;
}

  // 前面已经读过、空出来的空间长度 = readPos_
size_t Buffer::PrependableBytes() const {
    return readPos_;
}

// 返回当前可读数据的起始地址，但不移动读位置
const char* Buffer::Peek() const {
    return &buffer_[readPos_];
}

void Buffer::EnsureWritable(size_t len) {
    if (WritableBytes() < len) {
        MakeSpace(len);
    }
    assert(WritableBytes() >= len);
}

void Buffer::HasWritten(size_t len) {
    writePos_ += len;
}

char* Buffer::BeginWrite() {
    return BeginPtr() + writePos_;
}

const char* Buffer::BeginWrite() const {
    return BeginPtr() + writePos_;
}

  // 取走前 len 个字节
  // 本质不是删除数据，而是把 readPos_ 往后移动
void Buffer::Retrieve(size_t len) {
    assert(len <= ReadableBytes());

    // 如果取走的不是全部数据，只移动 readPos_
    if (len < ReadableBytes()){
    readPos_ += len;
    }

    // 如果刚好把可读数据全取完，就直接重置整个 Buffer
    else{
        RetrieveAll();
    }
}

  // 一直取到 end 这个位置
void Buffer::RetrieveUntil(const char* end) {
    assert(Peek() <= end);
    assert(end <= buffer_.data() + writePos_);

    Retrieve(static_cast<size_t>(end - Peek()));
}

  // 清空所有可读数据
  // 注意：这里不是清空 vector 的容量，而是把读写位置重置为 0
void Buffer::RetrieveAll() {
    readPos_ = 0;
    writePos_ = 0;

}

  // 取出全部可读数据，并转成 std::string 返回
std::string Buffer::RetrieveAllToStr() {
    std::string str(Peek(), ReadableBytes());
    RetrieveAll();
    return str;
}

  // 追加 std::string
void Buffer::Append(const std::string& str) {
    Append(str.data(), str.size());
}

void Buffer::Append(const void* data, size_t len){
    assert(data);
    Append(static_cast<const char*>(data), len);
}

void Buffer::Append(const Buffer& buff) {
    Append(buff.Peek(), buff.ReadableBytes());
}

void Buffer::Append(const char* str, size_t len) {
    assert(str);
    EnsureWritable(len);
    std::copy(str, str + len, BeginWrite());
    HasWritten(len);
}

ssize_t Buffer::ReadFd(int fd, int* saveErrno) {
    char buff[65535];
    struct iovec iov[2];

    const size_t writable = WritableBytes();
    iov[0].iov_base = BeginWrite();
    iov[0].iov_len = writable;
    iov[1].iov_base = buff;
    iov[1].iov_len = sizeof(buff);

    const ssize_t len = readv(fd, iov, 2);
    if (len < 0) {
        *saveErrno = errno;
    } else if (static_cast<size_t>(len) <= writable) {
        writePos_ += static_cast<size_t>(len);
    } else {
        writePos_ = buffer_.size();
        Append(buff, static_cast<size_t>(len) - writable);
    }
    return len;
}

ssize_t Buffer::WriteFd(int fd, int* saveErrno) {
    const size_t readSize = ReadableBytes();
    const ssize_t len = write(fd, Peek(), readSize);
    if (len < 0) {
        *saveErrno = errno;
        return len;
    }
    readPos_ += static_cast<size_t>(len);
    return len;
}

char* Buffer::BeginPtr() {
    return buffer_.data();
}

const char* Buffer::BeginPtr() const {
    return buffer_.data();
}

void Buffer::MakeSpace(size_t len) {
    if (WritableBytes() + PrependableBytes() < len) {
        buffer_.resize(writePos_ + len);
    } else {
        const size_t readable = ReadableBytes();
        std::copy(BeginPtr() + readPos_, BeginPtr() + writePos_, BeginPtr());
        readPos_ = 0;
        writePos_ = readable;
    }
}
