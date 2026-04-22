#include "log.h"
#include <cassert>

// 构造函数
Log::Log()
    : path_(nullptr),
      suffix_(nullptr),
      fp_(nullptr),
      isOpen_(false),
      isAsync_(false),
      level_(1),
      toDay_(0),
      lineCount_(0) {}

// 析构函数
Log::~Log() {

    if (writeThread_ && writeThread_->joinable() && deque_) {
        deque_->Close();
        writeThread_->join();
    }

    // 写线程结束后，安全关闭文件
    if (fp_) {
        std::lock_guard<std::mutex> locker(mtx_);
        flush();
        fclose(fp_);
    }
}

// 返回全局唯一的 Log 实例
Log* Log::Instance() {
    static Log inst;
    return &inst;
}

// init
void Log::init(int level, 
               const char* path, 
               const char* suffix, 
               int maxQueueSize) {

    isOpen_ = true;
    level_  = level;
    path_   = path;
    suffix_ = suffix;

    // maxQueueSize >0 才开启异步模式
    if (maxQueueSize > 0) {
        isAsync_ = true;

        if (writeThread_ && writeThread_->joinable()) {
            if (deque_) {
                deque_->Close();
            }
            writeThread_->join();
        }
        deque_ = std::make_unique<BlockDeque<std::string>>(maxQueueSize);

        // 创建后台写线程
        writeThread_ = std::make_unique<std::thread>(FlushLogThread);

    }
    else {
        isAsync_ = false;
    }

    lineCount_ = 0;

    // 获取当前时间，用来构造文件名和记录今天是几号
    time_t timer = time(nullptr);
    struct tm t;
    localtime_r(&timer, &t);

    toDay_ = t.tm_mday;

    // 提前创建目录
    mkdir(path_, 0777);

    char fileName[LOG_NAME_LEN] = {0};
    snprintf(fileName, LOG_NAME_LEN - 1, 
             "%s/%04d_%02d_%02d%s",
             path_,
             t.tm_year + 1900,
             t.tm_mon + 1,
             t.tm_mday,
             suffix_);

    // 加锁后打开文件
    {
        std::lock_guard<std::mutex> locker(mtx_);
        buff_.RetrieveAll();

        //如果已经打开了文件，先关闭旧的
        if (fp_) {
            fflush(fp_);
            fclose(fp_);
        }

        fp_ = fopen(fileName, "a");
        assert(fp_ != nullptr); // 如果还是打不开， 直接崩溃，方便找问题
    }
}

void Log::AppendLogLevelTitle_(int level) {
    switch (level) {
        case 0: buff_.Append("[debug]: ", 9); break;
        case 1: buff_.Append("[info] : ", 9); break;
        case 2: buff_.Append("[warn] : ", 9); break;
        case 3: buff_.Append("[error]: ", 9); break;
        default: buff_.Append("[info] : ", 9); break;
    }
}

void Log::write(int level, const char* format, ...) {
    
    // 获取当前时间，精确到微秒
    // timeval 有两个字段：tv_sec（秒）和 tv_usec（微秒）
    struct timeval now = {0, 0};
    gettimeofday(&now, nullptr);

    time_t tSec = now.tv_sec;
    struct tm t;
    localtime_r(&tSec, &t);  // 线程安全地把秒数转成年月日时分秒

    {
        std::unique_lock<std::mutex> locker(mtx_);
    
        // ---- 判断是否需要新建文件 ----
        // 条件1：日期变了（跨天）
        // 条件2：当前文件行数达到上限（MAX_LINES）
        if(toDay_ != t.tm_mday || 
            (lineCount_ > 1 && (lineCount_ % MAX_LINES == 0))) 
        {
            char newFile[LOG_NAME_LEN] = {0};
            char tail[36]              = {0};

            snprintf(tail, 36, "%04d_%02d_%02d", 
                     t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);

            // std::unique_lock<std::mutex> locker(mtx_);

            if(toDay_ != t.tm_mday) {
                // 跨天： 新文件名直接用新日期
                snprintf(newFile, LOG_NAME_LEN - 1, "%s/%s%s", path_, tail, suffix_);
                toDay_ = t.tm_mday;
                lineCount_ = 1;    
            } else {
                // 超行数：在日期后面加编号，如 2024_01_01-1.log
                snprintf(newFile, LOG_NAME_LEN - 1, 
                         "%s/%s-%d%s",
                         path_, tail, lineCount_ / MAX_LINES, suffix_);
            }
            fflush(fp_);
            fclose(fp_);
            fp_ = fopen(newFile, "a");
            assert(fp_ != nullptr);
        }
        ++lineCount_;
        // ---- 格式化日志内容到 buff_ ----

        // 第一段：时间戳
        // 格式：2024-01-01 12:00:00.123456
        // snprintf 把格式化结果写进 buff_.BeginWrite() 指向的地址
        // 返回值 n 是实际写入的字符数
        int n = snprintf(buff_.BeginWrite(), 128,
                         "%04d-%02d-%02d %02d:%02d:%02d.%06ld ",
                         t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                         t.tm_hour, t.tm_min, t.tm_sec, now.tv_usec);
        buff_.HasWritten(n);  // 告诉 Buffer：我写了 n 个字节，推进写指针

        // 第二段：级别前缀，如 "[info] :"
        AppendLogLevelTitle_(level);

        // 第三段：用户传入的消息
        // va_list 是可变参数列表的类型
        // va_start 初始化它，让它指向 format 后面的第一个可变参数
        va_list vaList;
        va_start(vaList, format);

        // vsnprintf 是 printf 的变体，接受 va_list 而不是 ...
        // 把格式化结果写进 buff_.BeginWrite()，最多写 WritableBytes() 个字节
        int m = vsnprintf(buff_.BeginWrite(), buff_.WritableBytes(),
                          format, vaList);
        va_end(vaList);  // 用完 va_list 必须调 va_end 释放资源
        
        // 钳制：保证 m 在合法范围内
        if (m < 0) m = 0;
        if (static_cast<size_t>(m) > buff_.WritableBytes()) {
            m = static_cast<size_t>(buff_.WritableBytes());
        }
        buff_.HasWritten(m);
        buff_.Append("\n\0", 2);  // 追加换行和字符串结束符

        // ---- 决定怎么写出去 ----
        if (isAsync_ && deque_ && !deque_->full()) {
            // 异步模式：把整条日志字符串推进队列
            // RetrieveAllToStr() 把 buff_ 里的内容转成 string 并清空 buff_
            deque_->push_back(buff_.RetrieveAllToStr());
        } else {
            // 同步模式（或队列满了）：直接写文件
            // fputs 把字符串写进 fp_ 指向的文件
            fputs(buff_.Peek(), fp_);
        }

        // 不管哪种模式，最后都清空 buff_，准备下次复用
        buff_.RetrieveAll();

    }

}

void Log::flush() {
    if (isAsync_ && deque_) {
        deque_->flush();  // 唤醒写线程
    }
    fflush(fp_);  // C 库级别的强制刷新
}

// GetLevel() / SetLevel()
// 运行时动态读写日志级别
int Log::GetLevel() {
    std::lock_guard<std::mutex> locker(mtx_);
    return level_;
}

void Log::SetLevel(int level) {
    std::lock_guard<std::mutex> locker(mtx_);
    level_ = level;
}

// 后台写线程的执行体
void Log::AsyncWrite_() {
    while (auto item = deque_->pop()) {
        std::lock_guard<std::mutex> locker(mtx_);
        fputs(item->c_str(), fp_);
        // item 是 optional<string>，item-> 解引用拿到 string
        // c_str() 把 string 转成 const char*，fputs 需要这个类型
    }
}

// 静态函数，作为 std::thread 的入口
void Log::FlushLogThread() {
    Log::Instance()->AsyncWrite_();
}