# TinyReactor — Claude Context

## 项目定位
从零手写的 C++ 高性能 Web 服务器，基于 Linux Reactor 网络模型。
目的：学习 C++ 系统编程，积累找工作的项目经验。

## 开发者背景
- C++ 中级水平，正在通过这个项目系统学习
- 熟悉 Python / Linux / Docker / CMake / Git
- 目标：找到高薪后端/系统开发岗位

## 开发环境
- 本地 Mac 编辑代码，Docker 容器内编译运行
- 容器：Ubuntu 22.04，g++ 11.4.0，CMake 3.22.1，C++17
- 启动容器：`docker compose up -d`
- 进入容器：`docker exec -it tinyreactor_dev bash`
- 编译：`cd /workspace/build && cmake .. && make`

## 项目结构
```
TinyReactor/
├── include/
│   ├── buffer.h
│   ├── blockdeque.h
│   └── log.h
├── src/
│   ├── buffer.cpp
│   ├── log.cpp
│   └── main.cpp
├── log/
├── CMakeLists.txt
├── Dockerfile
└── docker-compose.yml
```

## 已完成模块
- **Buffer**：基于 `std::vector<char>` 的动态缓冲区，readPos_ / writePos_ 双指针管理
- **BlockDeque**：模板类，`std::deque` + `mutex` + `condition_variable`，`pop()` 返回 `std::optional<T>`
- **Log**：单例异步日志，Buffer 做格式化工作台，BlockDeque 做异步队列，后台写线程消费

## 待实现模块
1. ~~Buffer~~
2. ~~Log~~
3. ThreadPool
4. HeapTimer
5. MySQL 连接池
6. HTTP 请求解析
7. HTTP 响应封装
8. Epoller
9. WebServer

## 代码规范
- C++17
- 成员变量下划线后缀：`level_`
- 私有函数下划线后缀：`AsyncWrite_()`
- 头文件保护：`#ifndef`
- 禁用拷贝：显式 `= delete`
- 常量：`static constexpr` 而不是 `#define`
- 时间函数：`localtime_r` 而不是 `localtime`
- 资源管理：`std::unique_ptr`，不用裸 `new`

## 参考项目
markparticle/WebServer 