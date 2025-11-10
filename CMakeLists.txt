cmake_minimum_required(VERSION 3.22.1)
project(Tcpserver)

set(CMAKE_CXX_STANDARD 20)

add_executable(Tcpserver main.cpp
        lock/lockers.h
        threadpool/threadpool.h)
