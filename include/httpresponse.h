#ifndef HTTPRESPONSE_H
#define HTTPRESPONSE_H

#include <cstddef>
#include <string>
#include <unordered_map>
#include <sys/stat.h>

#include "buffer.h"

class HttpResponse {
public:
    HttpResponse();
    ~HttpResponse();

    // 初始化一次响应要用到的上下文
    // srcDir：静态资源根目录
    // path：目标资源路径（例如 /index.html）
    // isKeepAlive：是否长连接
    // code：HTTP 状态码
    void Init(const std::string& srcDir,
              const std::string& path,
              bool isKeepAlive,
              int code);

    // 生成完整 HTTP 响应（状态行 + 响应头 + 响应体）
    void MakeResponse(Buffer& buff);

    // 释放 mmap 映射
    void UnmapFile();

    // 给 HttpConn::writev 使用
    char* File();
    size_t FileLen() const;

    int Code() const;

private:
    // 组装响应各部分
    void AddStateLine_(Buffer& buff);
    void AddHeader_(Buffer& buff);
    void AddContent_(Buffer& buff);

    // 错误页面处理
    void ErrorHtml_();
    void ErrorContent_(Buffer& buff, const std::string& message);

    // 根据文件后缀返回 Content-Type
    std::string GetFileType_() const;

private:
    int code_;                  // HTTP 状态码
    bool isKeepAlive_;          // 是否保持长连接

    std::string path_;          // 请求路径
    std::string srcDir_;        // 静态资源根目录

    char* mmFile_;              // mmap 后的文件起始地址
    struct stat mmFileStat_;    // 文件状态（大小、权限等）

    // 后缀 -> MIME 类型
    static const std::unordered_map<std::string, std::string> SUFFIX_TYPE;

    // 状态码 -> 状态文本
    static const std::unordered_map<int, std::string> CODE_STATUS;

    // 状态码 -> 默认错误页面路径
    static const std::unordered_map<int, std::string> CODE_PATH;
};

#endif
