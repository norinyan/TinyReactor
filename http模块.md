阶段1：接口冻结（先搭骨架）
先写 include/httprequest.h、httpresponse.h、httpconn.h、userservice.h，只定成员和函数签名；再写对应 .cpp 空实现，保证能编译通过。
理由：先锁边界，后面每一阶段只填实现，不反复改接口。

阶段2：静态 GET 最小闭环（你说的第一阶段）
先写 src/httprequest.cpp（只做请求行+请求头+Keep-Alive+Path 规范化）；再写 src/httpresponse.cpp（状态行/响应头/mmap静态文件/MIME）；最后写 src/httpconn.cpp（Read→Parse→HandleRequest→MakeResponse→Write）。
理由：先打通一条完整链路，浏览器能拿到 index.html/404.html。

阶段3：POST 解析能力（先不接数据库）
先扩展 httprequest.cpp 的 BODY、Content-Length、application/x-www-form-urlencoded、GetPost()；再在 httpconn.cpp 增加 POST 路由分发，但先用假逻辑返回成功/失败页。
理由：先把协议能力做稳，再接业务层，排错更清晰。

阶段4：登录/注册 + SQL
先写 src/userservice.cpp（QueryUser_/InsertUser_/EscapeString_/Login/Register，用 SqlConnRAII）；再把 httpconn.cpp 的 /login、/register 接到 UserService。
理由：数据库是外部依赖，放在协议闭环后接入，定位问题最快。

阶段5：稳定性与安全收尾
完善 httpconn.cpp 的 ET 下循环读写、短写续传、连接复用；完善 httprequest/httpresponse 的非法报文、路径穿越、错误码页面。
理由：功能跑通不等于可用，这一步决定你后面 WebServer 接入是否稳定。

开工前先定两条规则，后面会少很多坑：

HttpRequest::Parse 最好区分“成功 / 报文不完整 / 语法错误”，不要只用 bool。
静态资源路径必须做 .. 穿越防护，只允许在 srcDir 根目录内访问。


HTTP 模块设计大纲

目录结构：

include/
├── httprequest.h
├── httpresponse.h
├── httpconn.h
└── userservice.h

src/
├── httprequest.cpp
├── httpresponse.cpp
├── httpconn.cpp
└── userservice.cpp


============================================================
1. HttpRequest：协议层，请求解析
============================================================

文件：include/httprequest.h

class HttpRequest {
public:
    enum PARSE_STATE {
        REQUEST_LINE,
        HEADERS,
        BODY,
        FINISH
    };

public:
    HttpRequest();
    ~HttpRequest();

    void Init();

    bool Parse(Buffer& buff);

    std::string Method() const;
    std::string Path() const;
    std::string Version() const;
    std::string Body() const;

    std::string GetHeader(const std::string& key) const;
    std::string GetPost(const std::string& key) const;
    std::string GetPost(const char* key) const;

    bool IsKeepAlive() const;

private:
    bool ParseRequestLine_(const std::string& line);
    void ParseHeader_(const std::string& line);
    void ParseBody_(const std::string& line);

    void ParsePath_();
    void ParsePost_();
    void ParseFromUrlencoded_();

    static int ConvertHex_(char ch);

private:
    PARSE_STATE state_;

    std::string method_;
    std::string path_;
    std::string version_;
    std::string body_;

    std::unordered_map<std::string, std::string> headers_;
    std::unordered_map<std::string, std::string> post_;

    static const std::unordered_set<std::string> DEFAULT_HTML;
};


文件：src/httprequest.cpp

HttpRequest::HttpRequest();
HttpRequest::~HttpRequest();

void HttpRequest::Init();

bool HttpRequest::Parse(Buffer& buff);

std::string HttpRequest::Method() const;
std::string HttpRequest::Path() const;
std::string HttpRequest::Version() const;
std::string HttpRequest::Body() const;

std::string HttpRequest::GetHeader(const std::string& key) const;
std::string HttpRequest::GetPost(const std::string& key) const;
std::string HttpRequest::GetPost(const char* key) const;

bool HttpRequest::IsKeepAlive() const;

bool HttpRequest::ParseRequestLine_(const std::string& line);
void HttpRequest::ParseHeader_(const std::string& line);
void HttpRequest::ParseBody_(const std::string& line);

void HttpRequest::ParsePath_();
void HttpRequest::ParsePost_();
void HttpRequest::ParseFromUrlencoded_();

int HttpRequest::ConvertHex_(char ch);

const std::unordered_set<std::string> HttpRequest::DEFAULT_HTML;


============================================================
2. HttpResponse：协议层，响应生成
============================================================

文件：include/httpresponse.h

class HttpResponse {
public:
    HttpResponse();
    ~HttpResponse();

    void Init(const std::string& srcDir,
              const std::string& path,
              bool isKeepAlive,
              int code);

    void MakeResponse(Buffer& buff);
    void UnmapFile();

    char* File();
    size_t FileLen() const;

    int Code() const;

private:
    void AddStateLine_(Buffer& buff);
    void AddHeader_(Buffer& buff);
    void AddContent_(Buffer& buff);

    void ErrorHtml_();
    void ErrorContent_(Buffer& buff, const std::string& message);

    std::string GetFileType_();

private:
    int code_;
    bool isKeepAlive_;

    std::string path_;
    std::string srcDir_;

    char* mmFile_;
    struct stat mmFileStat_;

    static const std::unordered_map<std::string, std::string> SUFFIX_TYPE;
    static const std::unordered_map<int, std::string> CODE_STATUS;
    static const std::unordered_map<int, std::string> CODE_PATH;
};


文件：src/httpresponse.cpp

HttpResponse::HttpResponse();
HttpResponse::~HttpResponse();

void HttpResponse::Init(const std::string& srcDir,
                        const std::string& path,
                        bool isKeepAlive,
                        int code);

void HttpResponse::MakeResponse(Buffer& buff);
void HttpResponse::UnmapFile();

char* HttpResponse::File();
size_t HttpResponse::FileLen() const;

int HttpResponse::Code() const;

void HttpResponse::AddStateLine_(Buffer& buff);
void HttpResponse::AddHeader_(Buffer& buff);
void HttpResponse::AddContent_(Buffer& buff);

void HttpResponse::ErrorHtml_();
void HttpResponse::ErrorContent_(Buffer& buff, const std::string& message);

std::string HttpResponse::GetFileType_();

const std::unordered_map<std::string, std::string> HttpResponse::SUFFIX_TYPE;
const std::unordered_map<int, std::string> HttpResponse::CODE_STATUS;
const std::unordered_map<int, std::string> HttpResponse::CODE_PATH;


============================================================
3. HttpConn：连接层，一个客户端连接的一次请求-响应流程
============================================================

文件：include/httpconn.h

class HttpConn {
public:
    HttpConn();
    ~HttpConn();

    void Init(int fd, const sockaddr_in& addr);
    void Close();

    ssize_t Read(int* saveErrno);
    ssize_t Write(int* saveErrno);

    bool Process();

    int GetFd() const;
    int GetPort() const;
    const char* GetIP() const;
    sockaddr_in GetAddr() const;

    bool IsKeepAlive() const;
    int ToWriteBytes() const;

private:
    std::string HandleRequest_();

private:
    int fd_;
    sockaddr_in addr_;
    bool isClose_;

    Buffer readBuff_;
    Buffer writeBuff_;

    HttpRequest request_;
    HttpResponse response_;

    struct iovec iov_[2];
    int iovCnt_;

public:
    static bool isET;
    static const char* srcDir;
    static std::atomic<int> userCount;
};


文件：src/httpconn.cpp

HttpConn::HttpConn();
HttpConn::~HttpConn();

void HttpConn::Init(int fd, const sockaddr_in& addr);
void HttpConn::Close();

ssize_t HttpConn::Read(int* saveErrno);
ssize_t HttpConn::Write(int* saveErrno);

bool HttpConn::Process();

int HttpConn::GetFd() const;
int HttpConn::GetPort() const;
const char* HttpConn::GetIP() const;
sockaddr_in HttpConn::GetAddr() const;

bool HttpConn::IsKeepAlive() const;
int HttpConn::ToWriteBytes() const;

std::string HttpConn::HandleRequest_();

bool HttpConn::isET;
const char* HttpConn::srcDir;
std::atomic<int> HttpConn::userCount;


============================================================
4. UserService：业务层，登录 / 注册 / 数据库访问
============================================================

文件：include/userservice.h

class UserService {
public:
    UserService() = delete;
    ~UserService() = delete;

    static bool Login(const std::string& username,
                      const std::string& password);

    static bool Register(const std::string& username,
                         const std::string& password);

private:
    static bool QueryUser_(MYSQL* sql,
                           const std::string& username,
                           std::string* password);

    static bool InsertUser_(MYSQL* sql,
                            const std::string& username,
                            const std::string& password);

    static std::string EscapeString_(MYSQL* sql,
                                     const std::string& str);
};


文件：src/userservice.cpp

bool UserService::Login(const std::string& username,
                        const std::string& password);

bool UserService::Register(const std::string& username,
                           const std::string& password);

bool UserService::QueryUser_(MYSQL* sql,
                             const std::string& username,
                             std::string* password);

bool UserService::InsertUser_(MYSQL* sql,
                              const std::string& username,
                              const std::string& password);

std::string UserService::EscapeString_(MYSQL* sql,
                                       const std::string& str);


============================================================
整体工作流 ASCII 图
============================================================

客户端浏览器
    |
    |  HTTP 原始请求
    v
+----------------+
|   socket fd    |
+----------------+
    |
    | HttpConn::Read()
    v
+----------------+
|  readBuff_     |
+----------------+
    |
    | HttpRequest::Parse(readBuff_)
    v
+----------------+
|  HttpRequest   |
+----------------+
| method_        |
| path_          |
| version_       |
| headers_       |
| body_          |
| post_          |
+----------------+
    |
    | HttpConn::HandleRequest_()
    |
    | if POST /login.html
    | if POST /register.html
    v
+----------------+
|  UserService   |
+----------------+
| Login()        |
| Register()     |
| QueryUser_()   |
| InsertUser_()  |
+----------------+
    |
    | 返回业务结果 true / false
    v
+----------------+
|   HttpConn     |
+----------------+
| 决定 targetPath |
| /welcome.html  |
| /error.html    |
| /index.html    |
+----------------+
    |
    | response_.Init(srcDir, targetPath, keepAlive, code)
    v
+----------------+
|  HttpResponse  |
+----------------+
| code_          |
| path_          |
| srcDir_        |
| isKeepAlive_   |
| mmFile_        |
| mmFileStat_    |
+----------------+
    |
    | MakeResponse(writeBuff_)
    v
+----------------+
|  writeBuff_    |
+----------------+
    |
    | HttpConn::Write()
    v
+----------------+
|   socket fd    |
+----------------+
    |
    | HTTP 响应
    v
客户端浏览器


核心边界：

HttpRequest
    只解析请求
    不查 DB
    不决定最终返回页面

HttpResponse
    只生成响应
    不读 socket
    不处理业务

HttpConn
    负责连接流程
    负责调度 request / response / userservice

UserService
    只处理用户业务
    只负责登录 / 注册 / 数据库访问