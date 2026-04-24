#ifndef HTTPREQUEST_H
#define HTTPREQUEST_H
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <regex>
#include <cassert>
#include "buffer.h"

class HttpRequest {
public:
    // HTTP 解析状态机：请求行 -> 请求头 -> 请求体 -> 完成
    enum PARSE_STATE {
        REQUEST_LINE = 0,   // 开始解析
        HEADERS,            // 第一行读完
        BODY,               // 头部读完
        FINISH              // 全部读完
    };
    // 预留的 HTTP 处理结果码（后续给 HttpConn / HttpResponse 用）
    enum HTTP_CODE {
        NO_REQUEST = 0,     //  → 数据还不完整，等继续收
        GET_REQUEST,        //  → 正常的 GET 请求，可以处理
        BAD_REQUEST,        //  → 请求格式有问题
        NO_RESOURCE,        //  → 请求的文件不存在
        FORBIDDEN_REQUEST,  //  → 没有权限访问
        FILE_REQUEST,       //  → 请求的是静态文件
        INTERNAL_ERROR,     //  → 服务器内部出错
        CLOSED_CONNECTION   //  → 连接要关闭
    };
    HttpRequest() { Init(); }
    ~HttpRequest() = default;
    // 每次处理新请求前，重置内部状态
    void Init();
    // 从读缓冲里解析一个 HTTP 请求
    bool parse(Buffer& buff);
    // 对外读取解析结果
    std::string path() const;
    std::string& path();
    std::string method() const;
    std::string version() const;
    // 读取 POST 表单字段
    std::string GetPost(const std::string& key) const;
    std::string GetPost(const char* key) const;
    // 是否保持长连接
    bool IsKeepAlive() const;
private:
    // 三段解析函数
    bool ParseRequestLine_(const std::string& line);
    void ParseHeader_(const std::string& line);
    void ParseBody_(const std::string& line);
    // URL / POST 相关处理
    void ParsePath_();
    void ParsePost_();
    void ParseFromUrlencoded_();
    // 登录注册校验（连 MySQL）
    static bool UserVerify(const std::string& name, const std::string& pwd, bool isLogin);
    // URL 编码里十六进制字符转数值
    static int ConverHex(char ch);
private:
    PARSE_STATE state_;  // 当前解析状态
    std::string method_;
    std::string path_;
    std::string version_;
    std::string body_;
    std::unordered_map<std::string, std::string> header_;  // 请求头
    std::unordered_map<std::string, std::string> post_;    // POST 表单键值
    // 常用页面路由与登录注册路径标记
    static const std::unordered_set<std::string> DEFAULT_HTML;
    static const std::unordered_map<std::string, int> DEFAULT_HTML_TAG;
};
#endif
    