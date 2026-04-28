#include <iostream>
#include <string>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include "httpconn.h"
#include "sqlconnpool.h"

int main() {
    SqlConnPool* pool = SqlConnPool::Instance();
    pool->Init("mysql", 3306, "root", "root", "tinyreactor", 5);

    std::cout << "[SqlConnPool] free conn: "
              << pool->GetFreeConnCount() << std::endl;

    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) {
        std::cout << "[socketpair] create failed\n";
        pool->ClosePool();
        return 1;
    }

    int clientFd = fds[0];
    int serverFd = fds[1];

    std::string request =
        "POST /register.html HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: 35\r\n"
        "\r\n"
        "username=http_user1&password=123456";

    ssize_t writeLen = write(clientFd, request.data(), request.size());
    std::cout << "[Client] write bytes: " << writeLen << std::endl;

    shutdown(clientFd, SHUT_WR);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1316);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    HttpConn::srcDir = "../resources";
    HttpConn::isET = false;

    HttpConn conn;
    conn.Init(serverFd, addr);

    int saveErrno = 0;

    ssize_t readLen = conn.Read(&saveErrno);
    std::cout << "[HttpConn] read bytes: " << readLen << std::endl;

    if (!conn.Process()) {
        std::cout << "[HttpConn] process failed\n";
        conn.Close();
        close(clientFd);
        pool->ClosePool();
        return 1;
    }

    std::cout << "[HttpConn] response bytes: "
              << conn.ToWriteBytes() << std::endl;

    ssize_t sendLen = conn.Write(&saveErrno);
    std::cout << "[HttpConn] write bytes: " << sendLen << std::endl;

    char buff[4096];
    ssize_t recvLen = read(clientFd, buff, sizeof(buff) - 1);

    if (recvLen > 0) {
        buff[recvLen] = '\0';
        std::cout << "\n========== Client Recv ==========\n";
        std::cout << std::string(buff, static_cast<size_t>(recvLen)) << std::endl;
    } else {
        std::cout << "[Client] read response failed, len="
                  << recvLen << std::endl;
    }

    conn.Close();
    close(clientFd);

    std::cout << "[SqlConnPool] free conn after: "
              << pool->GetFreeConnCount() << std::endl;

    pool->ClosePool();

    return 0;
}
