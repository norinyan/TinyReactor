#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <memory>
#include <cassert>

class ThreadPool {
public:
    // 构造函数： 创建 threadCount 个工作线程，默认8个
    explicit ThreadPool(size_t threadCount = 8);

    // 析构函数： 关闭线程池， 等所有线程退出
    ~ThreadPool();

    // 禁用拷贝
    ThreadPool(const ThreadPool&)               = delete;
    ThreadPool& operator = (const ThreadPool&)  = delete;
    
    // 添加任务： 把可调用对象加入任务队列中
    // F&& 是万能引用， 支持 lambda、 函数指针、 bind 等所有可调用对象
    // std::forward 完美转发， 保留原始的左值/右值属性， 避免多余拷贝
    template<typename F>
    void AddTask(F&& task);

private:
    // 所有线程共享的核心数据，用 struct 打包在一起
    // 用 shared_ptr(智能指针) 管理：线程池对象和所有工作线程都持有它的引用
    // 保证线程池析构后工作线程仍能安全访问这块数据，直到线程真正退出
    struct Pool
    {
        std::mutex              mtx;
        std::condition_variable cond;
        bool                    isClosed;
        std::queue<std::function<void()>> tasks;

    };

    std::shared_ptr<Pool> pool_;
};
// 实现
inline ThreadPool::ThreadPool(size_t threadCount)
    : pool_(std::make_shared<Pool>()) {

    assert(threadCount > 0);
    pool_->isClosed = false;

    // 创建 threadCount 个工作线程
    for (size_t i = 0; i < threadCount; ++i){

        // 每个线程捕获 pool_的一份shared_ptr 拷贝
        // 这样即使 ThreadPool 对象析构， 线程还能安全访问 pool_
        std::thread([pool = pool_] {
            
            std::unique_lock<std::mutex> locker(pool->mtx);

            while (true)
            {
                if(!pool->tasks.empty()){
                    //队列有任务： 取出来， 解锁， 执行
                    auto task = std::move(pool->tasks.front());
                    pool->tasks.pop();

                    locker.unlock();    // 取完任务解锁，执行期间不占用锁
                    task();             // 执行任务
                    locker.lock();      // 执行完任务上锁，准备下一轮
                } else if (pool->isClosed){
                    // 队列空了且已经关闭：退出线程
                    break;
                } else {
                    // 队列空且未关闭： 释放锁并阻塞等待， 有任务或关闭时被唤醒
                    pool->cond.wait(locker);
                }
            }
            
        }).detach();    // detach:线程独立运行， 不需要手动join
    }
}
    
inline ThreadPool::~ThreadPool(){
    {
        std::lock_guard<std::mutex> locker(pool_->mtx);
        pool_->isClosed = true; // 置关闭标志

    }
    pool_->cond.notify_all();   // 唤醒所有阻塞的工作线程，让他们检查关闭标志退出
}

template<typename F>
void ThreadPool::AddTask(F&& task) {
    {
        std::lock_guard<std::mutex> locker(pool_->mtx);
        pool_->tasks.emplace(std::forward<F>(task));
    }
    pool_->cond.notify_one();       // 唤醒一个等待的工作线程来处理这个任务
}


#endif