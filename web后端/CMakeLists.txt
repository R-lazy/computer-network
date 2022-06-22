cmake_minimum_required(VERSION 3.16)
project(TinyServer)

set(CMAKE_CXX_STANDARD 14)
include_directories(include)
link_libraries(pthread)

add_executable(TinyServer main.cc include/common.h src/common/common.cc include/http_conn.h src/http_conn/http_conn.cc include/threadpool.h include/locker.h src/threadpool/threadpool.cc src/http_conn/state_machine.cc)
