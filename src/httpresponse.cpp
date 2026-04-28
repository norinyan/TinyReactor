#include "httpresponse.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

// ================================================================
// 1. 静态映射表：文件后缀 / 状态码 / 错误页面
// ================================================================
const std::unordered_map<std::string, std::string> HttpResponse::SUFFIX_TYPE = {
    {".html", "text/html"},
    {".css",  "text/css"},
    {".js",   "text/javascript"},
    {".png",  "image/png"},
    {".jpg",  "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".gif",  "image/gif"},
    {".ico",  "image/x-icon"},
    {".txt",  "text/plain"},
    {".xml",  "text/xml"},
    {".pdf",  "application/pdf"},
    {".mp4",  "video/mp4"}
};

const std::unordered_map<int, std::string> HttpResponse::CODE_STATUS = {
    {200, "OK"},
    {400, "Bad Request"},
    {403, "Forbidden"},
    {404, "Not Found"},
    {405, "Method Not Allowed"},
    {500, "Internal Server Error"}
};

const std::unordered_map<int, std::string> HttpResponse::CODE_PATH = {
    {400, "/400.html"},
    {403, "/403.html"},
    {404, "/404.html"},
    {405, "/405.html"},
    {500, "/error.html"}
};

// ================================================================
// 2. 构造 / 初始化 / 资源释放
//    HttpResponse 可能被连接复用，所以每次 Init 都要先释放旧 mmap
// ================================================================
HttpResponse::HttpResponse()
    : code_(-1),
      isKeepAlive_(false),
      path_(),
      srcDir_(),
      mmFile_(nullptr),
      mmFileStat_{} {}

HttpResponse::~HttpResponse() {
    UnmapFile();
}

void HttpResponse::Init(const std::string& srcDir,
                        const std::string& path,
                        bool isKeepAlive,
                        int code) {
    UnmapFile();

    srcDir_       = srcDir;
    path_         = path;
    isKeepAlive_  = isKeepAlive;
    code_         = code;
    mmFile_       = nullptr;
    mmFileStat_   = {};
}

void HttpResponse::UnmapFile() {
    if (mmFile_) {
        munmap(mmFile_, static_cast<size_t>(mmFileStat_.st_size));
        mmFile_ = nullptr;
    }
}

char* HttpResponse::File() {
    return mmFile_;
}

size_t HttpResponse::FileLen() const {
    return mmFile_ ? static_cast<size_t>(mmFileStat_.st_size) : 0;
}

int HttpResponse::Code() const {
    return code_;
}

// ================================================================
// 3. 响应主流程：确定状态码 -> 修正错误页 -> 组装响应
//    writeBuff_ 里只放状态行和响应头，文件内容通过 mmap + writev 发送
// ================================================================
void HttpResponse::MakeResponse(Buffer& buff) {
    std::string file = srcDir_ + path_;

    if (stat(file.c_str(), &mmFileStat_) < 0 || S_ISDIR(mmFileStat_.st_mode)) {
        code_ = 404;
    } else if (access(file.c_str(), R_OK) != 0) {
        code_ = 403;
    } else if (code_ == -1) {
        code_ = 200;
    }

    if (CODE_STATUS.find(code_) == CODE_STATUS.end()) {
        code_ = 400;
    }

    ErrorHtml_();

    AddStateLine_(buff);
    AddHeader_(buff);
    AddContent_(buff);
}

void HttpResponse::AddStateLine_(Buffer& buff) {
    std::string status = CODE_STATUS.find(code_)->second;

    buff.Append("HTTP/1.1 " + std::to_string(code_) + " " + status + "\r\n");
}

void HttpResponse::AddHeader_(Buffer& buff) {
    if (isKeepAlive_) {
        buff.Append("Connection: keep-alive\r\n");
        buff.Append("Keep-Alive: timeout=120, max=6\r\n");
    } else {
        buff.Append("Connection: close\r\n");
    }

    buff.Append("Content-Type: " + GetFileType_() + "\r\n");
}

void HttpResponse::AddContent_(Buffer& buff) {
    std::string file = srcDir_ + path_;

    if (stat(file.c_str(), &mmFileStat_) < 0 ||
        S_ISDIR(mmFileStat_.st_mode) ||
        access(file.c_str(), R_OK) != 0) {
        ErrorContent_(buff, "File Not Found");
        return;
    }

    int srcFd = open(file.c_str(), O_RDONLY);
    if (srcFd < 0) {
        ErrorContent_(buff, "File Not Found");
        return;
    }

    if (mmFileStat_.st_size == 0) {
        close(srcFd);
        buff.Append("Content-Length: 0\r\n\r\n");
        return;
    }

    void* mmRet = mmap(nullptr,
                       static_cast<size_t>(mmFileStat_.st_size),
                       PROT_READ,
                       MAP_PRIVATE,
                       srcFd,
                       0);
    close(srcFd);

    if (mmRet == MAP_FAILED) {
        mmFile_ = nullptr;
        ErrorContent_(buff, "File Map Error");
        return;
    }

    mmFile_ = static_cast<char*>(mmRet);

    buff.Append("Content-Length: " +
                std::to_string(static_cast<size_t>(mmFileStat_.st_size)) +
                "\r\n\r\n");
}

// ================================================================
// 4. 错误响应 / 文件类型
//    ErrorHtml_ 只负责把错误状态码映射到错误页面路径
//    ErrorContent_ 用于错误页面文件不存在时兜底
// ================================================================
void HttpResponse::ErrorHtml_() {
    auto it = CODE_PATH.find(code_);
    if (it != CODE_PATH.end()) {
        path_ = it->second;
    }
}

void HttpResponse::ErrorContent_(Buffer& buff, const std::string& message) {
    std::string body;
    std::string status = CODE_STATUS.find(code_)->second;

    body += "<html>\n";
    body += "<head><title>" + std::to_string(code_) + " " + status + "</title></head>\n";
    body += "<body>\n";
    body += "<h1>" + std::to_string(code_) + " " + status + "</h1>\n";
    body += "<p>" + message + "</p>\n";
    body += "<hr><p>TinyReactor</p>\n";
    body += "</body>\n";
    body += "</html>\n";

    buff.Append("Content-Length: " + std::to_string(body.size()) + "\r\n\r\n");
    buff.Append(body);
}

std::string HttpResponse::GetFileType_() const {
    size_t idx = path_.find_last_of('.');
    if (idx == std::string::npos) {
        return "text/plain";
    }

    std::string suffix = path_.substr(idx);
    auto it = SUFFIX_TYPE.find(suffix);

    if (it == SUFFIX_TYPE.end()) {
        return "text/plain";
    }

    return it->second;
}
