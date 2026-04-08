#ifndef BUFFER_H
#define BUFFER_H

#include <vector>
#include <string>
#include <cassert>

class Buffer {
public:
    explicit Buffer(size_t initBuffSize = 1024);
    ~Buffer() = default;   //析构函数，不需要手动 delete

    size_t WritableBytes() const;         //当前还能写多少字节
    size_t ReadableBytes() const;        //当前有多少字节可以读
    size_t PrependableBytes() const;     //前面已经“空出来”的空间有多少

    const char* Peek() const;
                            /*
                            - 返回当前可读数据的起始地址
                            - 但不移动读指针
                            可以理解为：
                            “看一眼现在从哪开始读”
                            */

    void Retrieve(size_t len);  //取走前 len 个字节
    void RetrieveUntil(const char* end);    //一直取到某个位置
    void RetrieveAll();                     //把可读数据全清掉
    std::string RetrieveAllToStr();         //把当前所有可读数据转成字符串并清空

    void Append(const std::string& str);    //往 Buffer 尾部追加数据，接 std::string
    void Append(const char* data, size_t len);//往 Buffer 尾部追加数据，接原始字节数组

private:
    std::vector<char> buffer_;
    size_t readPos_;
    size_t writePos_;

};

#endif