//
// Created by 28252 on 25-11-11.
//

#ifndef TCPSERVER_THREADPOOL_HPP
#define TCPSERVER_THREADPOOL_HPP
#include "list"
#include "cstdio"
#include "exception"
#include "pthread.h"
#include "..\lock\lockers.h"
#include "../cgi_mysql/sql_connection_pool.hpp"

template<typename T>
class threadpool {
public:
    threadpool(int act_model, sql_connection_pool* connpool,int thread_number = 8,int max_requests = 10000);
    ~threadpool();
    bool append(T* request, int state); //这个版本的append必须要有成员 m_state
    bool append_p(T* request);
private:
    //工作线程运行的函数，它不断从工作队列中取出任务并执行。
    static void* worker(void* arg);
    void run();
private:
    int m_thread_number; //线程数量
    int m_max_request; //请求队列中允许的最大请求数
    pthread_t* m_threads; //描述线程池的数组，其大小为m_thread_number
    std::list<T*> m_workqueue; //请求队列
    mutex m_queuelocker; //请求队列的互斥锁
    sem m_queuestat; //请求队列的信号量，Wait：相当于线程拿一个任务。 Post：相当于新添加一个任务。
    sql_connection_pool* m_connpool; //数据库连接池
    int m_actor_model; //模型切换
    bool m_stop;
};


#endif //TCPSERVER_THREADPOOL_HPP
