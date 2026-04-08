#ifndef BUFFER_H
#define BUFFER_H

#include <cstddef>
#include <vector>
#include <string>
#include <sys/types.h>
#include <cassert>

class Buffer {
public:
    explicit Buffer(size_t initBuffSize = 1024);
    ~Buffer() = default;   //析构函数，不需要手动 delete

    size_t WritableBytes() const;         //当前还能写多少字节
    size_t ReadableBytes() const;        //当前有多少字节可以读
    size_t PrependableBytes() const;     //前面已经“空出来”的空间有多少

    const char* Peek() const;

    void EnsureWritable(size_t len);   // 保证可写空间足够
    void HasWritten(size_t len);       // 写入完成后推进 writePos_

    char* BeginWrite();                // 返回当前可写区起始地址
    const char* BeginWrite() const;
    
    void Retrieve(size_t len);  //取走前 len 个字节
    void RetrieveUntil(const char* end);    //一直取到某个位置
    void RetrieveAll();                     //把可读数据全清掉
    std::string RetrieveAllToStr();         //把当前所有可读数据转成字符串并清空

    void Append(const std::string& str);    //往 Buffer 尾部追加数据，接 std::string
    void Append(const char* data, size_t len);//往 Buffer 尾部追加数据，接原始字节数组
    void Append(const void* data, size_t len);
    void Append(const Buffer& buff);

    ssize_t ReadFd(int fd, int* Errno);
    ssize_t WriteFd(int fd, int* Errno);



private:
    char* BeginPtr();
    const char* BeginPtr() const;
    void MakeSpace(size_t len);

    std::vector<char> buffer_;
    size_t readPos_;
    size_t writePos_;

};

#endif