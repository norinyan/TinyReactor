# TinyReactor

TinyReactor 是一个基于 **Reactor 模式** 实现的轻量级 C++ Web 服务器项目。

目标不是简单复刻，而是以工程化方式从零手写，在实践中学习 Linux 系统编程、网络编程、并发编程等核心知识。

## 技术栈

- **语言**：C++17
- **构建**：CMake
- **开发环境**：Docker（Ubuntu 22.04）
- **编译器**：g++ 11.4.0
- **数据库**：MySQL 8.0

## 开发环境

本项目全程在 Docker 容器中开发，保证跨平台环境一致。

```bash
# 启动容器
docker compose up -d

# 进入开发容器
docker exec -it tinyreactor_dev bash

# 编译
cd /workspace/build
cmake ..
make

# 运行
./server
```

## 目录结构

```
TinyReactor/
├── include/          # 头文件
│   ├── buffer.h
│   ├── blockdeque.h
│   └── log.h
├── src/              # 实现文件
│   ├── buffer.cpp
│   ├── log.cpp
│   └── main.cpp
├── log/              # 运行时日志文件（不进 git）
├── CMakeLists.txt
├── Dockerfile
└── docker-compose.yml
```

## 已实现模块

### Buffer — 动态缓冲区

基于 `std::vector<char>` 实现的动态字节缓冲区，用 `readPos_` 和 `writePos_` 两个位置标记把连续内存逻辑上划分为三段：已读区、可读区、可写区。

核心设计：
- `Append()` 往尾部追加数据，空间不足时自动扩容或整理碎片（`MakeSpace`）
- `Retrieve()` 消费数据，本质是移动 `readPos_`，不做内存拷贝
- `ReadFd()` 使用 `readv` 分散读，栈上临时缓冲区兜底，一次 syscall 读尽数据
- `WriteFd()` 把可读区数据直接 `write` 到 fd

在 Log 模块中作为格式化工作台复用，每条日志拼好后整体取走，Buffer 清空备用。

### BlockDeque — 线程安全阻塞队列

模板类，`std::deque<T>` 加锁封装，实现生产者/消费者模型。

核心设计：
- 两个 `condition_variable` 分别管理"不空"和"不满"两个等待条件，职责清晰
- `push_back()` 用 `unique_lock` + `wait`，队满自动阻塞
- `pop()` 返回 `std::optional<T>`（C++17），队空阻塞，队列关闭返回 `std::nullopt`，比输出参数写法更现代
- `Close()` 先加锁置关闭标志，再 `notify_all` 唤醒所有阻塞线程安全退出
- 显式禁用拷贝构造和拷贝赋值（内部持有 mutex，不可拷贝）

### Log — 异步日志系统

单例模式，支持同步和异步两种写入模式，对业务线程无感知。

核心设计：
- **异步模式**：业务线程调 `write()` 只把格式化好的字符串推进 `BlockDeque`，立即返回；后台写线程 `AsyncWrite_()` 循环消费队列写文件，两者完全解耦
- **同步模式**：`maxQueueSize = 0` 时不创建队列和写线程，直接 `fputs` 写文件
- `write()` 内部用 `Buffer` 拼装完整日志行（时间戳 + 级别前缀 + 用户消息），`vsnprintf` 返回值做钳制处理，防止越界
- 时间函数使用 `localtime_r`（线程安全），而非标准库的 `localtime`
- 按天自动切割日志文件，单文件超过 50000 行时按编号新建
- 对外暴露四个宏 `LOG_DEBUG / LOG_INFO / LOG_WARN / LOG_ERROR`，用 `do { } while(0)` 包裹保证在任意语法位置安全展开

日志格式：
```
2026-04-22 08:50:44.472649 [info] : server starting, port = 1316
```

### ThreadPool — 线程池

基于生产者/消费者模型实现的固定大小线程池，所有工作线程共享一个任务队列。

核心设计：
- 用 `struct Pool` 把锁、条件变量、关闭标志、任务队列打包，通过 `std::shared_ptr<Pool>` 在线程池对象和所有工作线程之间共享，保证线程池析构后工作线程仍能安全访问数据直到真正退出
- 工作线程逻辑直接写在构造函数的 lambda 里：持锁等待 → 有任务则取出解锁执行 → 执行完重新加锁回到等待；锁只保护队列操作，执行任务期间不持锁，保证并发
- `AddTask()` 用万能引用 `F&&` + `std::forward` 完美转发，支持 lambda、函数指针等所有可调用对象，零拷贝传入队列
- 工作线程用 `detach` 独立运行，生命周期由 `shared_ptr` 引用计数保证
- 纯头文件实现（`threadpool.h`），无需 `.cpp`

## 待实现模块

| 顺序 | 模块 | 状态 |
|------|------|------|
| 1 | Buffer | ✅ 完成 |
| 2 | Log | ✅ 完成 |
| 3 | ThreadPool | ✅ 完成 |
| 4 | HeapTimer | 🔲 待实现 |
| 5 | MySQL 连接池 | 🔲 待实现 |
| 6 | HTTP 请求解析 | 🔲 待实现 |
| 7 | HTTP 响应封装 | 🔲 待实现 |
| 8 | Epoller | 🔲 待实现 |
| 9 | WebServer | 🔲 待实现 |

## 参考说明

整体架构思路参考 [markparticle/WebServer](https://github.com/markparticle/WebServer)，代码基于 C++17 自行实现，结合工程规范做了调整和改进。