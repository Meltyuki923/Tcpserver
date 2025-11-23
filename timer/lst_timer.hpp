//
// Created by 28252 on 25-11-22.
//

#ifndef TCPSERVER_LST_TIMER_HPP
#define TCPSERVER_LST_TIMER_HPP

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <time.h>

//连接资源中用到了util_timer，提前声明
class util_timer;
//连接资源
struct client_data{
    //客户端socket地址
    sockaddr_in address;
    //socket文件描述符
    int sockfd;
    //定时器
    util_timer* timer;
};

//定时器类
class util_timer {
public:
    util_timer(): prev(nullptr),next(nullptr){}
public:
    //超时时间
    time_t expire;
    //回调函数指针：声明一个名为 cb_func 的函数指针，它可以指向 “无返回值、参数为客户端数据指针” 的函数。
    void (*cb_func)( client_data* );
    //连接资源
    client_data* user_data;
    //前驱定时器
    util_timer* prev;
    //后继定时器
    util_timer* next;
};

//定时器容器类（升序双向链表）
class sort_timer_lst{
public:
    sort_timer_lst();
    ~sort_timer_lst();
    //处理新节点添加到链表头部的情况
    void add_timer(util_timer *timer);
    //调整定时器，任务发生变化时，调整定时器在链表中的位置
    void adjust_timer(util_timer *timer);
    void del_timer(util_timer *timer);
    //定时任务处理函数
    void tick();

private:
    //私有成员，被公有成员add_timer和adjust_time调用
    //主要用于调整链表内部结点，处理新节点插入到链表中间 / 尾部的情况
    void add_timer(util_timer *timer, util_timer *lst_head);
    util_timer *head;
    util_timer *tail;
};
//定时器的运行时驱动工具，负责定时器的定时触发、信号处理、epoll 协同、超时任务调度，以及网络 IO 的辅助工具封装。
class Utils{
public:
    Utils(){}
    ~Utils(){};
    void init(int timeslot);

    //对文件描述符设置非阻塞
    int setnonblocking(int fd);

    //将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    //信号处理函数,此处仅仅发送信号。
    static void sig_handler(int sig);

    //设置信号函数,其中sigaction结构体变量调用上面的信号处理函数
    void addsig(int sig, void(*handler)(int), bool restart = true);

    //定时处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler();

    void show_error(int connfd, const char *info);

public:
    //信号处理的管道文件描述符（一对），用于将信号事件转化为 epoll 的 IO 事件
    static int *u_pipefd;
    sort_timer_lst m_timer_lst;
    static int u_epollfd;
    //定时器的时间片（单位通常为秒 / 毫秒），控制 SIGALRM 信号的触发频率
    int m_TIMESLOT;
};




#endif //TCPSERVER_LST_TIMER_HPP
