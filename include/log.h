#ifndef LOG_H
#define LOG_H

#include <mutex>         // std::mutex, std::lock_guard, std::unique_lock
#include <thread>        // std::thread
#include <memory>        // std::unique_ptr, std::make_unique
#include <string>        // std::string
#include <cstdarg>       // va_list, va_start, va_end，可变参数用
#include <sys/time.h>    // gettimeofday，获取带微秒的当前时间
#include <sys/stat.h>    // mkdir，创建目录

#include "buffer.h"
#include "blockdeque.h"

class Log {
public:

    // 获取全局唯一实例
    static Log* Instance();

    // 初始化：日志级别、存放目录、文件后缀、异步队列大小（0=同步）
    void init(int level,
              const char* path          = "./log",
              const char* suffix        = ".log",
              int         maxQueueSize  = 1024);

    // 写一条日志
    void write(int level, const char* format, ...);

    // 强制刷新缓冲区到文件
    void flush();

    // 获取 / 设置当前日志级别
    int GetLevel();
    void SetLevel(int level);

    // 日志系统是否已开启
    bool IsOpen() const { return isOpen_; }

    // 后台写线程的静态入口（传给 std::thread 用）
    static void FlushLogThread();

private:
        Log();
        ~Log();

        // 单例不允许拷贝
        Log(const Log&)                 = delete;
        Log& operator = (const Log&)    = delete;

        // 往 buff_ 追加级别前缀，如 "[info] :"
        void AppendLogLevelTitle_(int level);

        // 后台写线程执行体：循环从队列取字符串写文件
        void AsyncWrite_();

private:
    static constexpr int LOG_NAME_LEN = 256;     // 日志文件名最大长度
    static constexpr int MAX_LINES    = 50000;  // 单文件最大行数，超出则新建

    const char* path_;    // 日志目录路径
    const char* suffix_;  // 文件后缀
    FILE*       fp_;      // C 标准库的文件指针，fopen/fclose/fputs 都用它

    bool isOpen_;    // init() 调用后置 true，标记日志系统已启动
    bool isAsync_;   // true=异步，false=同步
    int  level_;     // 当前日志级别
    int  toDay_;     // 今天是几号，用来判断是否跨天需要新建文件
    int  lineCount_; // 当前文件已经写了多少行

    Buffer buff_;    // 格式化单条日志的临时工作台，写完就清空复用

    std::unique_ptr<BlockDeque<std::string>> deque_;        // 异步日志队列
    std::unique_ptr<std::thread>             writeThread_;  // 后台写线程
    
    std::mutex mtx_; // 互斥锁，保证同一时刻只有一个线程能操作 buff_ 和 fp_
    
};

// -------- 对外宏 --------
#define LOG_BASE(level, format, ...) \ 
    do { \ 
        Log* log = Log::Instance(); \
        if (log->IsOpen() && log->GetLevel() <= level) { \ 
            log->write(level, format, ##__VA_ARGS__); \
            log->flush(); \
        } \
    } while(0)

#define LOG_DEBUG(format, ...) do { LOG_BASE(0, format, ##__VA_ARGS__); } while(0)
#define LOG_INFO(format, ...)  do { LOG_BASE(1, format, ##__VA_ARGS__); } while(0)
#define LOG_WARN(format, ...)  do { LOG_BASE(2, format, ##__VA_ARGS__); } while(0)
#define LOG_ERROR(format, ...) do { LOG_BASE(3, format, ##__VA_ARGS__); } while(0)

#endif