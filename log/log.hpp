//
// Created by 28252 on 25-11-24.
//

#ifndef TCPSERVER_LOG_HPP
#define TCPSERVER_LOG_HPP
#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include <cstring>
#include "block_queue.hpp"

class log {
public:
    //c++11以后使用局部变量懒汉不用加锁
    static log* instance(){
        static log instance;
        return &instance;
    }
    //线程的入口函数用来。启动独立线程执行异步日志写入
    static void* flush_log_thread(void* args){
        log::instance()->async_write_log();;
    }
    //可选择的参数有日志文件、日志缓冲区大小、最大行数以及最长日志条队列
    bool init(const char *file_name, int close_log, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);
    //将输出内容按照标准格式整理
    void write_log(int level,const char* format,...);
    //强制刷新缓冲区
    void flush();

private:
    log();
    //定义virtual的原因：防止通过基类指针管理派生类对象，删除时会出现的资源泄漏。即delete指针时只会调用基类的析构，不会调用派生类的析构
    virtual ~log();
    void* async_write_log(){
        std::string signle_log;
        //从阻塞队列中取出一个日志string，写入文件
        while(m_log_queue->pop(signle_log)){
            m_mutex.lock();
            fputs(signle_log.c_str(),m_fp);
            m_mutex.unlock();
        }
    }

private:
    char dir_name[128]; //路径名
    char log_name[128]; //log文件名
    int m_split_lines;  //日志最大行数
    int m_log_buf_size; //日志缓冲区大小
    long long m_count;  //日志行数记录
    int m_today;        //因为按天分类,记录当前时间是那一天
    FILE *m_fp;         //打开log的文件指针
    char *m_buf;        //日志缓冲区
    block_queue<std::string> *m_log_queue; //阻塞队列
    bool m_is_async;                  //是否同步标志位
    mutex m_mutex;
    int m_close_log; //关闭日志
};

#define LOG_DEBUG(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(0, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_INFO(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_WARN(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_ERROR(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush();}


#endif //TCPSERVER_LOG_HPP
