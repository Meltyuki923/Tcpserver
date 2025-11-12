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
template<typename T>
class threadpool {
public:
    threadpool(int thread_number,int max_requests);
    ~threadpool();
    bool append(T* request);
    static void* worker(void* arg);
    void run();
private:
    int m_thread_number;
    int m_max_request;
    pthread_t* m_threads;
    std::list<T*> m_workqueue;
    mutex m_queuelocker; //任务队列的互斥锁
    sem m_queuestat; //任务队列的信号量，Wait：相当于线程拿一个任务。 Post：相当于新添加一个任务。
    bool m_stop;
};


#endif //TCPSERVER_THREADPOOL_HPP
