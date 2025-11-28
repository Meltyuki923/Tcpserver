//
// Created by 28252 on 25-11-11.
//

#include "threadpool.hpp"

template<typename T>
threadpool<T>::threadpool(int act_model, sql_connection_pool* connpool,int thread_number, int max_requests) :
m_actor_model(act_model),m_thread_number(thread_number),m_max_request(max_requests),m_stop(false),m_threads(nullptr),m_connpool(connpool){
    if(thread_number<=0 || max_requests<=0){
        throw std::exception();
    }
    m_threads = new pthread_t[m_thread_number];
    if(m_threads == nullptr){
        throw std::exception();
    }
    for(int i = 0;i<m_thread_number;i++){
        printf("Generate %d threads",i);
        if(pthread_create(m_threads,NULL,worker,this) != 0){
            delete [] m_threads;
            throw std::exception();
        }
        if(pthread_detach(m_threads[i]) != 0){
            delete [] m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
threadpool<T>::~threadpool() {
    delete [] m_threads;
    m_stop = true;
}

template<typename T>
bool threadpool<T>::append(T* request,int state) {
    m_queuelocker.lock();
    if(m_workqueue.size() >= m_max_request){
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state; //这句话的存在代表着request 这个模板参数必须有成员m_state。
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template<typename T>
bool threadpool<T>::append_p(T *request) {
    m_queuelocker.lock();
    if(m_workqueue.size() >= m_max_request){
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template<typename T>
void *threadpool<T>::worker(void *arg) {
    threadpool* pool = (threadpool*) arg;
    pool->run();
    return nullptr;
}


/*
 * 关于run()的模式部分的解析：
 * 这里我们支持Reactor模式对应m_actor_model == 1，以及Proactor模式对应 m_actor_model == 0
 * 因为Reactor模式需要先读写，所以我们引出了request->m_state来区分是读阶段还是写阶段，其中模板参数T* request 指的是httpcoon类
 * 当我们成功完成一个阶段，不管是否成功我们都会把request->improv 置 1 来表示该阶段完成。
 * 当某个阶段失败后，我们将request->timer_flag 置 1，表示将该连接交给定时器来处理，对该任务进行超时清理（如关闭套接字、释放资源）。
 */

template<typename T>
void threadpool<T>::run() {
    while(!m_stop){
        //当请求队列为空时，wait()会让该线程阻塞。直到通过append中的post唤醒线程,才会开始工作
        m_queuestat.wait();
        m_queuelocker.lock();
        if(m_workqueue.size() == 0){
            m_queuelocker.unlock();
            continue;
        }
        T* request = m_workqueue.front();
        m_queuelocker.unlock();
        if(request == nullptr){
            continue;
        }
        //Reactor逻辑：程序负责读写+执行逻辑（process()）
        if(m_actor_model == 1){
            //这里是读逻辑
            if(request->m_state == 0){
                //读成功
                if(request->read_once()){
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql, m_connpool);
                    request->process();
                }
                //读失败
                else{
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
            //这里是写逻辑
            else{
                //写成功
                if(request->write()){
                    request->improv = 1;
                }
                //写失败
                else{
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }
        //Proactor逻辑：程序只负责执行逻辑
        else{
            connectionRAII mysqlcon(&request->mysql, m_connpool);
            request->process();
        }
    }
}
