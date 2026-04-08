---
0. 当前固定操作流

进入开发容器：

```bash
docker exec -it tinyreactor_dev bash
```

进入后会看到类似：

```bash
root@xxxx:/workspace#
```

说明：
- `/workspace` 就是容器里的项目目录
- 它和 Mac 本地项目目录实时同步
- 我在本地 VSCode 改代码，容器里会立刻更新
- 我在容器里编译运行，本地也能看到生成内容

退出/停止：

```bash
# 退出容器（容器仍然在后台运行）
exit

# 停止容器
docker compose down

# 停止容器并删除 MySQL 数据卷
docker compose down -v
```

换电脑继续开发：

```bash
git clone https://github.com/yourname/TinyReactor.git
cd TinyReactor
docker compose up -d
docker exec -it tinyreactor_dev bash
```

这就是 Docker 开发的核心价值：
- 环境一致
- 换 macOS / Linux / Windows 都能直接接着干

---
1. 当前项目结构

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

当前状态：
- 项目骨架已建立
- GitHub 已上传
- Docker 开发环境已打通
- CMake 构建链路已打通

---
2. 搭建 Docker 开发环境

目标：
- 全程在 Docker 中开发
- 保证后续在 macOS / Linux / Windows 上环境一致

### 2.1 写 Dockerfile

```dockerfile
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    gdb \
    git \
    libmysqlclient-dev \
    vim \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

CMD ["/bin/bash"]
```

理解：
- `FROM ubuntu:22.04`：基础镜像，统一 Linux 开发环境
- `build-essential`：gcc / g++ / make，C++ 编译必需
- `cmake`：项目构建工具
- `gdb`：调试器
- `git`：版本管理
- `libmysqlclient-dev`：后面 MySQL 连接池要用
- `WORKDIR /workspace`：容器内工作目录

### 2.2 写 docker-compose.yml

```yaml
services:
  dev:
    build: .
    container_name: tinyreactor_dev
    volumes:
      - .:/workspace
    ports:
      - "1316:1316"
    stdin_open: true
    tty: true

  mysql:
    image: mysql:8.0
    container_name: tinyreactor_db
    environment:
      MYSQL_ROOT_PASSWORD: root
      MYSQL_DATABASE: tinyreactor
    ports:
      - "3307:3306"
    volumes:
      - db_data:/var/lib/mysql

volumes:
  db_data:
```

理解：
- `dev`：开发容器，用当前目录的 Dockerfile 构建
- `.:/workspace`：把本地项目目录挂载到容器 `/workspace`
- `1316:1316`：给后续 Web 服务器预留端口映射
- `mysql`：单独一个 MySQL 8.0 容器
- `3307:3306`：宿主机用 3307 访问容器里的 MySQL 3306
- `db_data`：命名卷，MySQL 数据持久化，删容器数据也不会马上丢

---
3. 构建并启动容器

执行：

```bash
docker compose up -d
```

验证：

```bash
docker ps
```

应看到两个容器：
- `tinyreactor_dev`
- `tinyreactor_db`

然后进入开发容器：

```bash
docker exec -it tinyreactor_dev bash
```

验证 C++ 环境：

```bash
g++ --version && cmake --version
```

当前验证结果：
- g++ 11.4.0
- CMake 3.22.1

说明：
- 这个环境足够支持 C++17 开发

---
4. 写最小可运行工程

### 4.1 写 CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)
project(TinyReactor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${PROJECT_SOURCE_DIR}/include)

add_executable(server src/main.cpp)
```

理解：
- `cmake_minimum_required(VERSION 3.16)`：最低要求 CMake 3.16
- `project(TinyReactor LANGUAGES CXX)`：定义项目名和语言
- `set(CMAKE_CXX_STANDARD 17)`：使用 C++17
- `CMAKE_CXX_STANDARD_REQUIRED ON`：必须用 C++17，不允许静默降级
- `include_directories(...)`：告诉编译器去 `include/` 找头文件
- `add_executable(server src/main.cpp)`：把 `src/main.cpp` 编译成可执行文件 `server`

### 4.2 写最简单的 main.cpp

```cpp
#include <iostream>

int main() {
    std::cout << "TinyReactor starting ..." << std::endl;
    return 0;
}
```

目的：
- 先不急着写服务器
- 先确认项目能编译、能运行、链路通了

---
5. 第一次编译运行

在容器里执行：

```bash
mkdir -p build
cd build
cmake ..
make
./server
```

运行结果：

```bash
TinyReactor starting ...
```

说明：
- 现在已经成功打通“本地写代码 + 容器内编译运行”这条链路

---
6. 理解这几个命令到底干了什么

### 6.1 `cmake ..`

含义：
- 当前在 `build/` 目录
- `..` 表示上一级项目根目录
- CMake 会去读根目录里的 `CMakeLists.txt`

做的事：
- 检测编译器
- 检测 C++ 标准支持情况
- 读取工程配置
- 在 `build/` 下生成构建文件

生成的典型内容：
- `Makefile`
- `CMakeCache.txt`
- `CMakeFiles/`

注意：
- 这一步**还没有真正编译源码**

### 6.2 `make`

含义：
- 根据 CMake 生成的 `Makefile` 开始真正编译

做的事：
- 把 `src/main.cpp` 编译成目标文件 `.o`
- 再把目标文件链接成最终可执行文件 `server`

### 6.3 `./server`

含义：
- 运行当前目录下的可执行文件 `server`

说明：
- `./` 表示当前目录
- 不写 `./`，shell 默认不会在当前目录找程序

### 6.4 为什么程序名叫 `server`

因为在 `CMakeLists.txt` 里写了：

```cmake
add_executable(server src/main.cpp)
```

其中第一个参数 `server` 就是最终生成的程序名。

---
7. 以后日常构建操作流

### 只改了 `.cpp/.h`

```bash
cd /workspace/build
make
./server
```

### 改了 `CMakeLists.txt`，或者新增/删除了源文件

```bash
cd /workspace/build
cmake ..
make
./server
```

### 第一次构建

```bash
mkdir -p build
cd build
cmake ..
make
./server
```

---
8. 当前阶段小结

到这里，已经完成：
- 项目基础目录初始化
- Docker 开发环境搭建
- MySQL 容器编排
- C++17 + CMake 基础工程建立
- 第一个可执行程序成功运行
- 本地编辑 + 容器编译运行链路打通

下一步：
- 开始写第一个正式模块：`Buffer`
- 后续实现顺序：
  1. Buffer
  2. Log
  3. ThreadPool
  4. HeapTimer
  5. MySQL 连接池
  6. HTTP 请求解析
  7. HTTP 响应封装
  8. Epoller
  9. WebServer
  
--------------从次往下开始buffer

我的理解：
1. buffer：
  本质上就是：
  用 readPos_ 和 writePos_ 两个位置标记，把一块连续内存逻辑上分成 已读区、可读区、可写区，然后提供对这块数据的
  读、写、取出、查看、扩容 等操作。

  Buffer 是一个基于 std::vector<char> 的动态缓冲区，
  用 readPos_ 和 writePos_ 标记有效数据范围，
  实现对网络数据的缓存、读取、追加、清理和扩容管理。

```buffer结构图
  Buffer
  ├── 数据
  │   ├── vector<char> buffer_
  │   ├── size_t readPos_
  │   └── size_t writePos_
  │
  ├── 查询接口
  │   ├── ReadableBytes()
  │   ├── WritableBytes()
  │   └── PrependableBytes()
  │
  ├── 读取相关
  │   ├── Peek()
  │   ├── Retrieve()
  │   ├── RetrieveAll()
  │   └── RetrieveAllToStr()
  │
  ├── 写入相关
  │   └── Append()
  │
  └── 后续扩展
      ├── EnsureWritable()
      ├── MakeSpace()
      ├── ReadFd()
      └── WriteFd()

```

在include 中touch buffer.h
写

```buffer.h
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
```

在src创建buffer.cpp写

```buffer.cpp


```