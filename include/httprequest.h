#ifndef HTTPREQUEST_H
#define HTTPREQUEST_H

#include <string>
#include <unordered_map>
#include <unordered_set>

#include "buffer.h"

class HttpRequest {
public:
    // 内部流程
    enum PARSE_STATE {
        REQUEST_LINE = 0,
        HEADERS,
        BODY,
        FINISH
    };

    // 对外解析结果
    enum PARSE_RESULT {
        NO_REQUEST = 0,   // 报文还不完整，继续读
        GET_REQUEST,      // 解析完成
        BAD_REQUEST       // 语法错误
    };

public:
    HttpRequest();
    ~HttpRequest();

    void Init();

    PARSE_RESULT Parse(Buffer& buff);   // 从缓冲区解析并填充成员数据

    // 读取已经解析好的成员值
    std::string Method() const;
    std::string Path() const;
    std::string Version() const;
    std::string Body() const;

    std::string GetHeader(const std::string& key) const;    // 查 headers_（请求头键值对）
    std::string GetPost(const std::string& key) const;      // 查 post_（POST 表单键值对）
    std::string GetPost(const char* key) const;             // 调用时可直接传 C 字符串字面量，少一次手动构造 std::string

    bool IsKeepAlive() const;   // 判断长链接

private:
    bool ParseRequestLine_(const std::string& line);        // 解析首行（方法、路径、版本）
    bool ParseHeader_(const std::string& line);             // 解析每个请求头行
    bool ParseBody_(const std::string& line);               // 解析请求体原始内容

    void ParsePath_();                                      // 规范化路径（例如短路径补 .html）
    void ParsePost_();                                      // 判断并触发 POST 参数解析
    void ParseFromUrlencoded_();                            // 把 a=1&b=2 这种表单体解码进 post_

    static int ConvertHex_(char ch);                        // 给 %2F 这类 URL 编码解码用

private:
    PARSE_STATE state_;         // 当前走到状态机哪一步

    // 解析结果存储
    std::string method_;
    std::string path_;
    std::string version_;
    std::string body_;

    // 两类键值数据存储
    std::unordered_map<std::string, std::string> headers_;
    std::unordered_map<std::string, std::string> post_;

    // 常用静态页面短路径集合，如 /login -> /login.html
    static const std::unordered_set<std::string> DEFAULT_HTML;
};

#endif
