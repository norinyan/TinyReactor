cmake_minimum_required(VERSION 3.16)
project(TinyReactor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories(${PROJECT_SOURCE_DIR}/include)

add_executable(server 
    src/main.cpp
    src/buffer.cpp
    src/log.cpp
    src/heaptimer.cpp
    src/sqlconnpool.cpp
    src/httprequest.cpp
    src/httpresponse.cpp
    src/httpconn.cpp
    src/userservice.cpp
    src/epoller.cpp
    src/webserver.cpp
)
target_link_libraries(server mysqlclient)