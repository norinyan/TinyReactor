#include "httprequest.h"

#include <algorithm>
#include <cstdlib>

// ================================================================
// 1. 静态成员：短路径集合
//    例如浏览器请求 /login，内部转换成 /login.html
// ================================================================
const std::unordered_set<std::string> HttpRequest::DEFAULT_HTML = {
    "/index",
    "/register",
    "/login",
    "/welcome",
    "/video",
    "/picture"
};

// ================================================================
// 2. 构造 / 初始化
//    每次复用 HttpRequest 前，都要清空上一次解析留下的数据
// ================================================================
HttpRequest::HttpRequest() {
    Init();
}

HttpRequest::~HttpRequest() = default;

void HttpRequest::Init() {
    method_.clear();
    path_.clear();
    version_.clear();
    body_.clear();

    headers_.clear();
    post_.clear();

    state_ = REQUEST_LINE;
}

// ================================================================
// 3. 主解析流程：按状态机解析 请求行 -> 请求头 -> 请求体
//    返回 NO_REQUEST：报文还不完整
//    返回 GET_REQUEST：解析完成
//    返回 BAD_REQUEST：报文格式错误
// ================================================================
HttpRequest::PARSE_RESULT HttpRequest::Parse(Buffer& buff) {
    const char CRLF[] = "\r\n";

    if (buff.ReadableBytes() <= 0) {
        return NO_REQUEST;
    }

    while (buff.ReadableBytes() > 0 && state_ != FINISH) {
        if (state_ == BODY) {
            int contentLen = 0;
            std::string len = GetHeader("Content-Length");

            if (!len.empty()) {
                contentLen = std::atoi(len.c_str());
            }

            if (contentLen < 0) {
                return BAD_REQUEST;
            }

            if (buff.ReadableBytes() < static_cast<size_t>(contentLen)) {
                return NO_REQUEST;
            }

            std::string body(buff.Peek(), static_cast<size_t>(contentLen));
            buff.Retrieve(static_cast<size_t>(contentLen));

            if (!ParseBody_(body)) {
                return BAD_REQUEST;
            }

            continue;
        }

        const char* readEnd = buff.Peek() + buff.ReadableBytes();

        const char* lineEnd = std::search(buff.Peek(),
                                          readEnd,
                                          CRLF,
                                          CRLF + 2);

        if (lineEnd == readEnd) {
            return NO_REQUEST;
        }

        std::string line(buff.Peek(), lineEnd);
        buff.RetrieveUntil(lineEnd + 2);

        switch (state_) {
        case REQUEST_LINE:
            if (!ParseRequestLine_(line)) {
                return BAD_REQUEST;
            }
            break;

        case HEADERS:
            if (line.empty()) {
                std::string len = GetHeader("Content-Length");
                if (!len.empty() && std::atoi(len.c_str()) > 0) {
                    state_ = BODY;
                } else {
                    state_ = FINISH;
                }
            } else if (!ParseHeader_(line)) {
                return BAD_REQUEST;
            }
            break;

        default:
            break;
        }
    }

    return state_ == FINISH ? GET_REQUEST : NO_REQUEST;
}

// ================================================================
// 4. 具体解析细节：请求行 / 请求头 / 请求体 / 路径 / POST 表单
// ================================================================
bool HttpRequest::ParseRequestLine_(const std::string& line) {
    size_t firstSpace = line.find(' ');
    size_t secondSpace = line.find(' ', firstSpace + 1);

    if (firstSpace == std::string::npos ||
        secondSpace == std::string::npos) {
        return false;
    }

    method_ = line.substr(0, firstSpace);
    path_ = line.substr(firstSpace + 1, secondSpace - firstSpace - 1);

    std::string httpVersion = line.substr(secondSpace + 1);
    if (httpVersion.find("HTTP/") != 0) {
        return false;
    }

    version_ = httpVersion.substr(5);

    if (method_ != "GET" && method_ != "POST") {
        return false;
    }

    if (version_ != "1.1" && version_ != "1.0") {
        return false;
    }

    if (path_.empty() || path_[0] != '/') {
        return false;
    }

    if (path_.find("..") != std::string::npos) {
        return false;
    }

    ParsePath_();
    state_ = HEADERS;
    return true;
}

bool HttpRequest::ParseHeader_(const std::string& line) {
    size_t idx = line.find(':');
    if (idx == std::string::npos) {
        return false;
    }

    std::string key = line.substr(0, idx);
    std::string value = line.substr(idx + 1);

    while (!value.empty() && value[0] == ' ') {
        value.erase(value.begin());
    }

    headers_[key] = value;
    return true;
}

bool HttpRequest::ParseBody_(const std::string& line) {
    body_ = line;
    ParsePost_();
    state_ = FINISH;
    return true;
}

void HttpRequest::ParsePath_() {
    if (path_ == "/") {
        path_ = "/index.html";
        return;
    }

    if (DEFAULT_HTML.count(path_) > 0) {
        path_ += ".html";
    }
}

void HttpRequest::ParsePost_() {
    if (method_ == "POST" &&
        GetHeader("Content-Type") == "application/x-www-form-urlencoded") {
        ParseFromUrlencoded_();
    }
}

void HttpRequest::ParseFromUrlencoded_() {
    std::string key;
    std::string value;
    std::string* cur = &key;

    for (size_t i = 0; i < body_.size(); ++i) {
        char ch = body_[i];

        if (ch == '=') {
            cur = &value;
        } else if (ch == '&') {
            if (!key.empty()) {
                post_[key] = value;
            }
            key.clear();
            value.clear();
            cur = &key;
        } else if (ch == '+') {
            cur->push_back(' ');
        } else if (ch == '%' && i + 2 < body_.size()) {
            int high = ConvertHex_(body_[i + 1]);
            int low = ConvertHex_(body_[i + 2]);

            if (high >= 0 && low >= 0) {
                cur->push_back(static_cast<char>(high * 16 + low));
                i += 2;
            }
        } else {
            cur->push_back(ch);
        }
    }

    if (!key.empty()) {
        post_[key] = value;
    }
}

int HttpRequest::ConvertHex_(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

std::string HttpRequest::Method() const {
    return method_;
}

std::string HttpRequest::Path() const {
    return path_;
}

std::string HttpRequest::Version() const {
    return version_;
}

std::string HttpRequest::Body() const {
    return body_;
}

std::string HttpRequest::GetHeader(const std::string& key) const {
    auto it = headers_.find(key);
    return it == headers_.end() ? "" : it->second;
}

std::string HttpRequest::GetPost(const std::string& key) const {
    auto it = post_.find(key);
    return it == post_.end() ? "" : it->second;
}

std::string HttpRequest::GetPost(const char* key) const {
    if (!key) {
        return "";
    }
    return GetPost(std::string(key));
}

bool HttpRequest::IsKeepAlive() const {
    return version_ == "1.1" && GetHeader("Connection") == "keep-alive";
}
