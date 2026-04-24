#include "httprequest.h"

#include <algorithm>

#include "log.h"
#include "sqlconnpool.h"
#include "sqlconnRAII.h"

using std::smatch;
using std::string;

// 常用页面：访问这些短路径时，自动补成 .html
const std::unordered_set<string> HttpRequest::DEFAULT_HTML{
    "/index", "/register", "/login",
    "/welcome", "/video", "/picture"
};

// 登录/注册页面打标：0=注册，1=登录
const std::unordered_map<string, int> HttpRequest::DEFAULT_HTML_TAG{
    {"/register.html", 0},
    {"/login.html",    1}
};

// 初始化一个全新的请求解析状态
void HttpRequest::Init() {
    method_.clear();
    path_.clear();
    version_.clear();
    body_.clear();
    state_ = REQUEST_LINE;
    header_.clear();
    post_.clear();
}

// 是否保持长连接
bool HttpRequest::IsKeepAlive() const {
    if (auto it = header_.find("Connection"); it != header_.end()) {
        return it->second == "keep-alive" && version_ == "1.1";
    }
    return false;
}

// 解析 HTTP 请求（状态机）
bool HttpRequest::parse(Buffer& buff) {
    if (buff.ReadableBytes() == 0) {
        return false;
    }

    static const char CRLF[] = "\r\n";

    while (buff.ReadableBytes() > 0 && state_ != FINISH) {
        // BODY 阶段不按 CRLF 切行，按 Content-Length 读取
        if (state_ == BODY) {
            size_t contentLen = 0;
            if (auto it = header_.find("Content-Length"); it != header_.end()) {
                try {
                    contentLen = static_cast<size_t>(std::stoul(it->second));
                } catch (...) {
                    return false;
                }
            } else {
                // 没有 Content-Length，按当前可读区全部当 body
                contentLen = buff.ReadableBytes();
            }

            if (buff.ReadableBytes() < contentLen) {
                // 数据还没收完整，等下次继续 parse
                return false;
            }

            string line(buff.Peek(), buff.Peek() + contentLen);
            ParseBody_(line);
            buff.Retrieve(contentLen);
            continue;
        }

        const char* begin   = buff.Peek();
        const char* end     = buff.Peek() + buff.ReadableBytes();
        const char* lineEnd = std::search(begin, end, CRLF, CRLF + 2);

        // 当前还没有完整的一行（没有 \r\n）
        if (lineEnd == end) {
            break;
        }

        string line(begin, lineEnd);

        switch (state_) {
        case REQUEST_LINE:
            if (!ParseRequestLine_(line)) {
                return false;
            }
            ParsePath_();
            break;
        case HEADERS:
            ParseHeader_(line);
            break;
        default:
            break;
        }

        buff.RetrieveUntil(lineEnd + 2);  // 跳过 "\r\n"
    }

    LOG_DEBUG("[HttpRequest] method=%s, path=%s, version=%s",
              method_.c_str(), path_.c_str(), version_.c_str());

    return state_ == FINISH;
}

// 处理路径：/ -> /index.html，常见短路径补 .html
void HttpRequest::ParsePath_() {
    if (path_ == "/") {
        path_ = "/index.html";
        return;
    }

    if (DEFAULT_HTML.find(path_) != DEFAULT_HTML.end()) {
        path_ += ".html";
    }
}

// 解析请求行：METHOD SP PATH SP HTTP/VERSION
bool HttpRequest::ParseRequestLine_(const string& line) {
    std::regex patten("^([^ ]+) ([^ ]+) HTTP/([^ ]+)$");
    smatch subMatch;

    if (!std::regex_match(line, subMatch, patten)) {
        LOG_ERROR("HttpRequest::ParseRequestLine_ error, line=%s", line.c_str());
        return false;
    }

    method_  = subMatch[1];
    path_    = subMatch[2];
    version_ = subMatch[3];
    state_   = HEADERS;
    return true;
}

// 解析头部：Key: Value
void HttpRequest::ParseHeader_(const string& line) {
    if (line.empty()) {
        // 头部结束：有 Content-Length 进入 BODY，否则请求完成（常见 GET）
        if (header_.count("Content-Length")) {
            state_ = BODY;
        } else {
            state_ = FINISH;
        }
        return;
    }

    std::regex patten("^([^:]+): ?(.*)$");
    smatch subMatch;
    if (std::regex_match(line, subMatch, patten)) {
        header_[subMatch[1]] = subMatch[2];
    }
}

// 解析请求体
void HttpRequest::ParseBody_(const string& line) {
    body_ = line;
    ParsePost_();
    state_ = FINISH;
    LOG_DEBUG("HttpRequest body len=%d", static_cast<int>(body_.size()));
}

// 十六进制字符转数值
int HttpRequest::ConverHex(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

// POST 请求处理入口
void HttpRequest::ParsePost_() {
    if (method_ != "POST") return;

    auto it = header_.find("Content-Type");
    if (it == header_.end()) return;

    // 兼容 "application/x-www-form-urlencoded; charset=UTF-8"
    if (it->second.find("application/x-www-form-urlencoded") == string::npos) return;

    ParseFromUrlencoded_();

    if (auto tagIt = DEFAULT_HTML_TAG.find(path_); tagIt != DEFAULT_HTML_TAG.end()) {
        int tag = tagIt->second;  // 0=register, 1=login
        if (tag == 0 || tag == 1) {
            bool isLogin = (tag == 1);
            if (UserVerify(post_["username"], post_["password"], isLogin)) {
                path_ = "/welcome.html";
            } else {
                path_ = "/error.html";
            }
        }
    }
}

// 解析 x-www-form-urlencoded：a=1&b=2，支持 '+' 和 '%xx'
void HttpRequest::ParseFromUrlencoded_() {
    if (body_.empty()) return;

    string key;
    string value;
    bool inKey = true;

    for (size_t i = 0; i < body_.size(); ++i) {
        char ch = body_[i];

        if (ch == '=' && inKey) {
            inKey = false;
            continue;
        }

        if (ch == '&') {
            post_[key] = value;
            key.clear();
            value.clear();
            inKey = true;
            continue;
        }

        char decoded = ch;
        if (ch == '+') {
            decoded = ' ';
        } else if (ch == '%' && i + 2 < body_.size()) {
            int high = ConverHex(body_[i + 1]);
            int low  = ConverHex(body_[i + 2]);
            if (high >= 0 && low >= 0) {
                decoded = static_cast<char>((high << 4) | low);
                i += 2;
            }
        }

        if (inKey) {
            key.push_back(decoded);
        } else {
            value.push_back(decoded);
        }
    }

    if (!key.empty()) {
        post_[key] = value;
    }

    for (const auto& kv : post_) {
        LOG_DEBUG("post: %s = %s", kv.first.c_str(), kv.second.c_str());
    }
}

// 用户校验：isLogin=true 登录；false 注册
bool HttpRequest::UserVerify(const string& name, const string& pwd, bool isLogin) {
    if (name.empty() || pwd.empty()) {
        return false;
    }

    MYSQL* sql = nullptr;
    SqlConnRAII raii(&sql, SqlConnPool::Instance());
    if (!sql) {
        LOG_ERROR("UserVerify: get sql conn failed");
        return false;
    }

    std::string safeName(name.size() * 2 + 1, '\0');
    std::string safePwd(pwd.size() * 2 + 1, '\0');

    unsigned long safeNameLen = mysql_real_escape_string(
        sql, safeName.data(), name.c_str(), static_cast<unsigned long>(name.size()));
    unsigned long safePwdLen = mysql_real_escape_string(
        sql, safePwd.data(), pwd.c_str(), static_cast<unsigned long>(pwd.size()));

    safeName.resize(safeNameLen);
    safePwd.resize(safePwdLen);

    if (isLogin) {
        // 登录：查密码比对
        string query = string("SELECT password FROM user WHERE username='") + safeName + "' LIMIT 1";
        if (mysql_query(sql, query.c_str()) != 0) {
            LOG_ERROR("UserVerify login query failed: %s", mysql_error(sql));
            return false;
        }

        MYSQL_RES* res = mysql_store_result(sql);
        if (!res) {
            LOG_ERROR("UserVerify login store_result failed: %s", mysql_error(sql));
            return false;
        }

        bool ok = false;
        if (MYSQL_ROW row = mysql_fetch_row(res); row && row[0]) {
            ok = (pwd == row[0]);
        }
        mysql_free_result(res);
        return ok;
    }

    // 注册：先查重，再插入
    string query = string("SELECT 1 FROM user WHERE username='") + safeName + "' LIMIT 1";

    if (mysql_query(sql, query.c_str()) != 0) {
        LOG_ERROR("UserVerify register check failed: %s", mysql_error(sql));
        return false;
    }

    MYSQL_RES* res = mysql_store_result(sql);
    if (!res) {
        LOG_ERROR("UserVerify register store_result failed: %s", mysql_error(sql));
        return false;
    }

    bool exists = (mysql_fetch_row(res) != nullptr);
    mysql_free_result(res);
    if (exists) {
        return false;
    }

    query = string("INSERT INTO user(username, password) VALUES('") + safeName + "','" + safePwd + "')";
    if (mysql_query(sql, query.c_str()) != 0) {
        LOG_ERROR("UserVerify register insert failed: %s", mysql_error(sql));
        return false;
    }

    return true;
}

std::string HttpRequest::path() const {
    return path_;
}

std::string& HttpRequest::path() {
    return path_;
}

std::string HttpRequest::method() const {
    return method_;
}

std::string HttpRequest::version() const {
    return version_;
}

std::string HttpRequest::GetPost(const std::string& key) const {
    assert(!key.empty());
    if (auto it = post_.find(key); it != post_.end()) {
        return it->second;
    }
    return "";
}

std::string HttpRequest::GetPost(const char* key) const {
    assert(key != nullptr);
    if (auto it = post_.find(std::string(key)); it != post_.end()) {
        return it->second;
    }
    return "";
}
