# TinyReactor

TinyReactor 是一个基于 **Reactor 模式** 实现的轻量级 C++ Web 服务器项目。

这个项目的目标不是简单复刻，而是以工程化方式，从零手写并逐步实现一个可运行的高性能服务器，在实践中学习：

- Linux 网络编程
- Epoll I/O 多路复用
- Reactor 事件驱动模型
- 线程池与并发编程
- 定时器与连接管理
- HTTP 请求解析与响应生成
- MySQL 连接池
- CMake 工程化构建
- Docker 跨平台一致开发环境

## 项目目标

- 用 **C++17** 手写实现一个高性能 Web 服务器
- 使用 **CMake** 管理构建流程
- 使用 **Docker** 统一 macOS / Linux / Windows 的开发环境
- 在“边做边学”的过程中沉淀自己的理解、笔记和工程经验

## 当前开发环境

本项目全程在 Docker 容器中开发，当前已验证环境如下：

- Ubuntu 22.04
- g++ 11.4.0
- CMake 3.22.1
- MySQL 8.0

## 目录结构

```text
TinyReactor
├── CMakeLists.txt
├── Dockerfile
├── README.md
├── docker-compose.yml
├── include
├── log.md
└── src
```

后续会逐步扩展为更规范的工程目录结构。

## 快速开始

### 1. 启动容器

```bash
docker compose up -d
```

### 2. 查看容器是否启动成功

```bash
docker ps
```

应该能看到两个容器：

- `tinyreactor_dev`：开发容器
- `tinyreactor_db`：MySQL 容器

### 3. 进入开发容器

```bash
docker exec -it tinyreactor_dev bash
```

进入后，项目目录位于：

```bash
/workspace
```

宿主机与容器之间通过 volume 同步代码：

- 你在本地编辑代码，容器里会同步更新
- 你在容器里编译运行，本地也能看到生成的内容

### 4. 第一次编译运行

```bash
mkdir -p build
cd build
cmake ..
make
./server
```

如果成功，应该看到：

```bash
TinyReactor starting ...
```

## 日常开发操作流

### 修改普通源码文件后

```bash
cd /workspace/build
make
./server
```

### 修改 CMakeLists.txt 或新增源文件后

```bash
cd /workspace/build
cmake ..
make
./server
```

### 退出容器

```bash
exit
```

### 停止容器

```bash
docker compose down
```

### 停止容器并删除 MySQL 数据卷

```bash
docker compose down -v
```

## 计划实现的模块

按这个顺序逐步实现：

1. Buffer 缓冲区
2. Log 日志系统
3. ThreadPool 线程池
4. HeapTimer 定时器
5. MySQL 连接池
6. HTTP 请求解析
7. HTTP 响应封装
8. Epoller 封装
9. WebServer 核心调度

## 参考说明

本项目的整体学习路线与部分架构思路参考了优秀开源项目：

- [markparticle/WebServer](https://github.com/markparticle/WebServer)

但本项目会坚持：

- 自己手写代码
- 使用 C++17 重构实现
- 使用 CMake 进行工程化管理
- 使用 Docker 统一开发环境
- 根据自己的理解调整目录结构与实现细节

## 项目状态

当前已完成：

- 项目基础目录初始化
- Docker 开发环境搭建
- MySQL 容器编排
- CMake 基础构建链路打通
- 第一个可执行程序成功运行

后续将从 **Buffer 模块** 开始逐步实现。
