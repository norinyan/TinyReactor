#include <iostream>
#include "buffer.h"
#include "log.h"
#include "threadpool.h"
#include "heaptimer.h"
#include <unistd.h>

int main() {
    // ---- 测 Buffer ----
    Buffer buff;
    buff.Append("hello world", 11);
    std::cout << "[Buffer] readable: " << buff.ReadableBytes() << std::endl;
    buff.Retrieve(6);
    std::cout << "[Buffer] after Retrieve(6): " 
              << std::string(buff.Peek(), buff.ReadableBytes()) << std::endl;

    // ---- 测 Log ----
    Log::Instance()->init(0, "/workspace/log", ".log", 1024);
    LOG_DEBUG("debug msg");
    LOG_INFO("info msg, port = %d", 1316);
    LOG_WARN("warn msg");
    LOG_ERROR("error msg");

    // ---- 测 ThreadPool ----
    ThreadPool pool(4);
    for (int i = 0; i < 8; ++i) {
        pool.AddTask([i] {
            std::cout << "task " << i << " running\n";
        });
    }
    sleep(1);

    // ---- 测 HeapTimer ----
    HeapTimer timer;

    // 添加三个连接，超时时间不同
    timer.add(1, 100, [] { std::cout << "[HeapTimer] fd=1 timeout, closed\n"; });
    timer.add(2, 200, [] { std::cout << "[HeapTimer] fd=2 timeout, closed\n"; });
    timer.add(3, 300, [] { std::cout << "[HeapTimer] fd=3 timeout, closed\n"; });

    std::cout << "[HeapTimer] next tick in: " << timer.getNextTick() << "ms\n";

    // 等 150ms，fd=1 应该超时，fd=2 fd=3 还没到
    usleep(150000);
    timer.tick();

    // 刷新 fd=2 的超时时间，再加 300ms
    timer.update(2, 300);

    // 等 200ms，fd=3 应该超时
    usleep(200000);
    timer.tick();

    // 等 200ms，fd=2 超时
    usleep(200000);
    timer.tick();

    return 0;
}