阶段1：接口冻结（先搭骨架）
先写 include/webserver.h，只定成员变量和函数签名；再写 src/webserver.cpp 空实现，保证能编译通过。
理由：WebServer 是总调度中心，先把它和 Epoller、HttpConn、ThreadPool、HeapTimer 的边界定清楚。

阶段2：服务器初始化
实现 socket、bind、listen、端口复用、非阻塞 fd、监听 fd 加入 Epoller。
理由：先让服务器真正监听端口，但暂时不处理复杂 HTTP 流程。

阶段3：事件循环
实现 Start() 主循环：timer_.tick()、epoller_.Wait(timeout)、遍历就绪事件。
理由：WebServer 的核心就是 Reactor 事件循环。

阶段4：连接管理
实现 AcceptClient_、CloseConn_、AddClient_，把 clientFd 和 HttpConn 绑定起来。
理由：监听 fd 只负责接入新连接，客户端 fd 才对应具体 HttpConn。

阶段5：读写事件处理
实现 DealRead_、DealWrite_、OnRead_、OnWrite_、OnProcess_。
理由：把 EPOLLIN / EPOLLOUT 事件分发给 HttpConn，并根据处理结果切换监听事件。

阶段6：线程池接入
把耗时的连接读写和请求处理投递到 ThreadPool。
理由：主线程专心等事件，工作线程负责具体连接处理。

阶段7：定时器接入
新连接加入 HeapTimer，有读写活动时刷新定时器，超时后关闭连接。
理由：避免空闲连接长期占用 fd 和内存。

阶段8：真实浏览器测试
用浏览器或 curl 访问 index/login/register 页面，验证静态资源和登录注册链路。
理由：所有底层模块最终都要在真实 WebServer 中闭环。

WebServer 模块设计大纲

目录结构：

include/
└── webserver.h

src/
└── webserver.cpp


============================================================
1. WebServer：Reactor 总调度中心
============================================================

WebServer 负责把所有已完成模块组装起来：

1. Log
   负责运行日志。

2. SqlConnPool
   给 UserService 提供数据库连接。

3. ThreadPool
   负责执行连接读写和 HTTP 处理任务。

4. HeapTimer
   负责关闭超时连接。

5. Epoller
   负责监听 fd 事件。

6. HttpConn
   负责单个客户端连接的 HTTP 读、处理、写。


============================================================
2. 核心职责
============================================================

WebServer 应该做：

1. 初始化监听 socket
   socket / bind / listen / setsockopt

2. 设置 fd 非阻塞
   listenFd 和 clientFd 都要设置非阻塞

3. 注册事件到 Epoller
   listenFd 监听 EPOLLIN
   clientFd 监听 EPOLLIN / EPOLLOUT

4. 维护连接数组
   fd -> HttpConn

5. 运行事件循环
   epoller_.Wait(timer_.getNextTick())

6. 分发事件
   listenFd 可读 -> accept 新连接
   clientFd 可读 -> HttpConn::Read / Process
   clientFd 可写 -> HttpConn::Write

7. 管理超时
   新连接 add timer
   活跃连接 update timer
   超时回调 CloseConn_


============================================================
3. 不属于 WebServer 的职责
============================================================

WebServer 不应该做：

1. 不直接解析 HTTP
   交给 HttpRequest / HttpConn。

2. 不直接生成 HTTP 响应
   交给 HttpResponse / HttpConn。

3. 不直接访问数据库
   交给 UserService / SqlConnPool。

4. 不自己实现 epoll 细节
   交给 Epoller。

5. 不自己实现任务队列
   交给 ThreadPool。


============================================================
4. 事件流
============================================================

浏览器请求
    |
    v
listenFd 触发 EPOLLIN
    |
    v
WebServer::AcceptClient_()
    |
    v
clientFd 加入 Epoller + HttpConn 初始化 + Timer 添加
    |
    v
clientFd 触发 EPOLLIN
    |
    v
HttpConn::Read()
    |
    v
HttpConn::Process()
    |
    v
生成响应后 ModFd(clientFd, EPOLLOUT)
    |
    v
clientFd 触发 EPOLLOUT
    |
    v
HttpConn::Write()
    |
    v
写完：
    keep-alive -> ModFd(clientFd, EPOLLIN)
    非 keep-alive -> CloseConn_()


============================================================
5. 第一版实现原则
============================================================

1. 先实现单进程 Reactor 主循环
   不急着做复杂配置系统。

2. 先支持静态页面和已有登录注册链路
   不新增 HTTP 功能。

3. fd 必须非阻塞
   ET 模式下尤其重要。

4. 连接关闭统一走 CloseConn_
   避免 fd 泄漏和重复 close。

5. WebServer 只做调度
   业务细节继续留在已有模块里。


============================================================
6. 完成标准
============================================================

1. 能启动服务器并监听指定端口。

2. 浏览器访问 / 能返回 index.html。

3. 访问不存在资源能返回错误页。

4. 登录 / 注册 POST 链路仍然可用。

5. keep-alive 能复用连接。

6. 空闲连接超时后能自动关闭。

7. Ctrl+C 或进程退出时资源能正常释放。
