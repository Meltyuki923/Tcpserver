cmake_minimum_required(VERSION 3.22.1)
project(Tcpserver)

set(CMAKE_CXX_STANDARD 20)

add_executable(Tcpserver main.cpp
        lock/lockers.h
        threadpool/threadpool.cpp
        threadpool/threadpool.hpp
        http_conn/httpconn.cpp
        http_conn/httpconn.hpp
        timer/lst_timer.cpp
        timer/lst_timer.hpp
        log/block_queue.cpp
        log/block_queue.hpp
)
