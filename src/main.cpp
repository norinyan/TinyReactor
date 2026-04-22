#include <iostream>
#include "buffer.h"
#include "log.h"
#include "threadpool.h"
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

    // 测threadpool
    ThreadPool pool(4);
    
    for (int i = 0; i < 8; ++i) {
        pool.AddTask([i] {
            std::cout << "task " << i << " running\n";
        });
    }
    
    sleep(1);  // 等线程执行完

    return 0;
}