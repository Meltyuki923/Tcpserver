//
// Created by 28252 on 25-11-24.
//

#ifndef TCPSERVER_BLOCK_QUEUE_HPP
#define TCPSERVER_BLOCK_QUEUE_HPP

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "../lock/lockers.h"
template <class T>
class block_queue {
public:
    block_queue(int max_size = 1000);
    ~block_queue();
    void clear();
    bool full();
    bool empty();
    bool front(T& value);
    bool back(T& value);
    int size();
    int max_size();
    bool push(const T& item);
    bool pop(T& item);
    bool pop(T& item, int ms_timeout);

private:
    mutex m_mutex;
    cond m_cond;

    T *m_array;
    int m_size;
    int m_max_size;
    int m_front;
    int m_back;

};




#endif //TCPSERVER_BLOCK_QUEUE_HPP
