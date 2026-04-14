#ifndef BLOCKDEQUE_H
#define BLOCKDEQUE_H

#include <deque>
#include <mutex>
#include <condition_variable>
#include <cassert>
#include <optional>

template<typename T>
class BlockDeque {
public:
    explicit BlockDeque(size_t maxSize = 1000);
    ~BlockDeque();          // 析构函数

    BlockDeque(const BlockDeque&) = delete;
    BlockDeque& operator = (const BlockDeque&) = delete;

    void push_back(T item); //让外部线程能往队列里放数据（从队尾）
    std::optional<T> pop(); //让外部线程能从队列里取数据（从队首）
    void flush();
    void Close();

    // 让外部能查看队列状态 ， 空， 满， 当前队列里有多少个元素， 容量
    bool empty();
    bool full();
    size_t size();
    size_t capacity();

private:
    std::deque<T>       deque_;             // 仓库
    size_t              maxSize_;           // 仓库最大存多少
    bool                isClosed_;          // 是否关门
    std::mutex          mtx_;               // 保护共享数据，保证同一时刻只有一个线程能操作队列
    std::condition_variable condConsumer_;  //消费者等待 用的条件变量，当队列有数据时通知消费者
    std::condition_variable condProducer_;  //生产者等待 用的条件变量，当队列有空位时通知生产者
};

// 构造函数
template<typename T>
BlockDeque<T>::BlockDeque(size_t maxSize)
    : maxSize_(maxSize), isClosed_(false) {
        assert(maxSize > 0);
    }

// 析构函数
template<typename T>
BlockDeque<T>::~BlockDeque() {
    Close();
}

// empty, full, size, capacity
template<typename T>
bool BlockDeque<T>::empty() {
    std::lock_guard<std::mutex> locker(mtx_);   //查询先加锁，lock_guard加锁方式
    return deque_.empty();
}

template<typename T>
bool BlockDeque<T>::full() {
    std::lock_guard<std::mutex> locker(mtx_);
    return deque_.size() >= maxSize_;
}

template<typename T>
size_t BlockDeque<T>::size() {
    std::lock_guard<std::mutex> locker(mtx_);
    return deque_.size();
}

template<typename T>
size_t BlockDeque<T>::capacity() {
    return maxSize_;
}

/*
push_back()：加锁，满了就等，关闭就返回，否则尾插并通知消费者
pop()：加锁，空了就等，醒来若仍为空则返回空值，否则头取并通知生产者
Close()：设置关闭标志，唤醒所有等待线程
*/


// close.     1. isclosed 标志置true  2.唤醒所有生产者，消费者 
template<typename T>
void BlockDeque<T>::Close() {
    {
        std::lock_guard<std::mutex> locker(mtx_);
        isClosed_ = true;
    }
    condConsumer_.notify_all();
    condProducer_.notify_all();
}

//push back
template<typename T>
void BlockDeque<T>::push_back(T item) {
    std::unique_lock<std::mutex> locker(mtx_);
    condProducer_.wait(locker, [this] {
        return deque_.size() < maxSize_ || isClosed_;
    });
    if (isClosed_) return;
    deque_.push_back(std::move(item));
    condConsumer_.notify_one(); 
}

// pop
template<typename T>
std::optional<T> BlockDeque<T>::pop() {
    std::unique_lock<std::mutex> locker(mtx_);
    condConsumer_.wait(locker, [this] {
        return !deque_.empty() || isClosed_;
    });
    if (deque_.empty()) return std::nullopt;
    T item = std::move(deque_.front());
    deque_.pop_front();
    condProducer_.notify_one();
    return item;
}

// flush：强制唤醒所有消费者，让写线程赶紧处理队列里剩余的数据
template<typename T>
void BlockDeque<T>::flush() {
    condConsumer_.notify_all();
}

#endif