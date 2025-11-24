//
// Created by 28252 on 25-11-24.
//

#include "block_queue.hpp"

template<class T>
block_queue<T>::block_queue(int max_size) {
    if(max_size <= 0){
        exit(-1);
    }
    m_max_size = max_size;
    m_array = new T[max_size];
    m_size = 0;
    m_front = -1;
    m_back = -1;
}

template<class T>
void block_queue<T>::clear() {
    m_mutex.lock();
    m_size = 0;
    m_front = -1;
    m_back = -1;
    m_mutex.unlock();
}

template<class T>
block_queue<T>::~block_queue() {
    m_mutex.lock();
    if(m_array != nullptr){
        delete []m_array;
    }
    m_mutex.unlock();
}

template<class T>
bool block_queue<T>::full() {
    m_mutex.lock();
    if(m_size>=m_max_size){
        m_mutex.unlock();
        return true;
    }
    m_mutex.unlock();
    return false;
}

template<class T>
bool block_queue<T>::empty() {
    m_mutex.lock();
    if(m_size == 0){
        m_mutex.unlock();
        return true;
    }
    m_mutex.unlock();
    return false;
}

template<class T>
bool block_queue<T>::front(T &value) {
    m_mutex.lock();
    if(m_size > 0){
        value = m_array[m_front];
        m_mutex.unlock();
        return true;
    }
    m_mutex.unlock();
    return false;
}

template<class T>
bool block_queue<T>::back(T &value) {
    m_mutex.lock();
    if(m_size > 0){
        value = m_array[m_back];
        m_mutex.unlock();
        return true;
    }
    m_mutex.unlock();
    return false;
}

template<class T>
int block_queue<T>::size() {
    int tmp = 0;
    m_mutex.lock();
    tmp = m_size;
    m_mutex.unlock();
    return tmp;
}

template<class T>
int block_queue<T>::max_size() {
    int tmp = 0;
    m_mutex.lock();
    tmp = m_max_size;
    m_mutex.unlock();
    return tmp;
}

//上菜了
template<class T>
bool block_queue<T>::push(const T &item) {
    m_mutex.lock();
    if(m_size>=m_max_size){
        m_mutex.unlock();
        //有菜,开吃
        m_cond.broadcast();
        return false;
    }
    m_back = (m_back + 1) % m_max_size;
    m_array[m_back] = item;
    m_size++;
    //上菜
    m_cond.broadcast();
    m_mutex.unlock();
    return true;
}

template<class T>
bool block_queue<T>::pop(T &item) {
    m_mutex.lock();
    //通过while (m_size <= 0)循环判断队列是否为空（而非if），解决条件变量的虚假唤醒问题（这是该函数的一个亮点）；
    while(m_size <= 0){
        if(!m_cond.wait(m_mutex.get())){
            m_mutex.unlock();
            return false;
        }
    }
    m_front = (m_front + 1) % m_max_size;
    item = m_array[m_front];
    m_size--;
    m_mutex.unlock();
    return false;
}

//增加超时处理
template<class T>
bool block_queue<T>::pop(T &item, int ms_timeout) {
    struct timespec t = {0, 0};
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    m_mutex.lock();
    if (m_size <= 0)
    {
        t.tv_sec = now.tv_sec + ms_timeout / 1000;
        t.tv_nsec = (ms_timeout % 1000) * 1000;
        if (!m_cond.timewait(m_mutex.get(), t))
        {
            m_mutex.unlock();
            return false;
        }
    }

    if (m_size <= 0)
    {
        m_mutex.unlock();
        return false;
    }

    m_front = (m_front + 1) % m_max_size;
    item = m_array[m_front];
    m_size--;
    m_mutex.unlock();
    return true;
}

