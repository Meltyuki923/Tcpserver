//
// Created by 28252 on 25-11-22.
//

#include "lst_timer.hpp"
#include "../http_conn/httpconn.hpp"
//具体的回调函数
void cb_func(client_data* user_data){
    //取消监听用户fd上的事件
    epoll_ctl(Utils::u_epollfd,EPOLL_CTL_DEL,user_data->sockfd,0);
    //assert仅在调试模式下起作用。这段代码的意思是如果user_data为NULL，则程序会直接终止并打印错误信息。
    //但是如果在release版本下user_data为空，则可能会引发很大的问题。（如空指针解引用)
    assert(user_data);
    //关闭fd
    close(user_data->sockfd);
    httpconn::m_user_count--;
}

sort_timer_lst::sort_timer_lst() {
    head = nullptr;
    tail = nullptr;
}

sort_timer_lst::~sort_timer_lst() {
    util_timer* tmp = head;
    while(tmp){
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}

void sort_timer_lst::add_timer(util_timer *timer) {
    if(timer == nullptr) return;
    //头指针为空，则让头指针和尾指针都指向新添加的timer
    if(!head){
        head = timer;
        tail = timer;
        return;
    }
    //如果新节点的超时时间小于头节点的超时时间（说明新节点是最早超时的）
    if(timer->expire <= head->expire){
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    //处理新节点插入到链表中间 / 尾部的情况
    add_timer(timer,head);
}

void sort_timer_lst::adjust_timer(util_timer *timer) {
    if(timer == nullptr) return;
    util_timer* tmp = timer->next;
    //当处于尾部，或者更新后的值仍小于next的值，则不用更改
    if(tmp == nullptr || timer->expire <= tmp->expire){
        return;
    }
    //以上的情况之外，如果处于头部,则重新插入
    if(timer == head){
        head = head->next;
        head->prev = nullptr;
        timer->next = nullptr;
        timer->prev = nullptr;
        //重新插入
        add_timer(timer,head);
    }
    //处于中间时
    else{
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        timer->next = nullptr;
        timer->prev = nullptr;
        add_timer(timer,timer->next);
    }
}

void sort_timer_lst::del_timer(util_timer *timer) {
    if(timer == nullptr) return;
    //只有一个节点
    if((timer == head) && (timer == tail)){
        delete timer;
        head = nullptr;
        tail = nullptr;
        timer = nullptr;
        return;
    }

    if(timer == head){
        head = head->next;
        head->prev = nullptr;
        delete timer;
        timer = nullptr;
        return;
    }

    if(timer == tail){
        timer->prev = nullptr;
        delete timer;
        timer = nullptr;
        return;
    }
    //在中间
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
    timer = nullptr;
}
//定时任务处理函数：执行大于超时时间timer的cb_func()，并删除timer。然后继续遍历直到
void sort_timer_lst::tick() {
    if(head == nullptr) return;
    //获取当前时间
    time_t cur = time(NULL);
    util_timer* tmp = head;
    //遍历tmp
    while(tmp){
        //head的时间都没有超时，后面的都不会超时
        if(cur < tmp->expire){
            break;
        }
        //当前定时器超时，执行cb_func()。
        //注意超时的只会是头节点,所以每次得重置头节点
        tmp->cb_func(tmp->user_data);
        head = tmp->next;
        //避免空指针解引用
        if(head){
            head->prev = nullptr;
        }
        delete tmp;
        tmp = head;
    }
}
//在中间或尾部进行添加
//para:util_timer *lst_head并不是指当前双向列表的头节点，而是指从这个节点开始遍历。
void sort_timer_lst::add_timer(util_timer *timer, util_timer *lst_head) {
    //此时必须定义两个指针,使程序更加易懂
    util_timer* tmp_prev = lst_head;
    util_timer* tmp = tmp_prev->next;
    while(tmp){
        //timer插入的是tmp的prev
        if(timer->expire <= tmp->expire){
            tmp_prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = tmp_prev;
            break;
        }
        tmp_prev = tmp;
        tmp = tmp->next;
    }
    //遍历完发现目标定时器需要添加到尾部
    if(tmp == nullptr){
        tmp_prev->next = timer;
        timer->prev = tmp_prev;
        timer->next = nullptr;
        tail = timer;
    }
}


void Utils::init(int timeslot) {
    m_TIMESLOT = timeslot;
}

int Utils::setnonblocking(int fd) {
    int old_option = fcntl(fd,F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd,F_SETFL,new_option);
    return old_option;
}
//TRIGMode ： 控制是 ET 还是 LT 模式
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;
    //ET
    if(TRIGMode == 1){
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    }
    //LT
    else{
        event.events = EPOLLIN | EPOLLRDHUP;
    }
    if(one_shot){
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
    setnonblocking(fd);
}

void Utils::sig_handler(int sig) {
    //errno 是Linux的全局错误码变量，此处保留原错误码，使函数可重入
    int save_errno = errno;
    int msg = sig;
    send(u_pipefd[1],(char*)&msg,1,0);
    errno = save_errno;
}

void Utils::addsig(int sig, void (*handler)(int), bool restart) {
    struct sigaction sa;
    memset(&sa,'\0',sizeof sa);
    //设置信号处理函数，就是上面写的那个
    sa.sa_handler = handler;
    if(restart){
        sa.sa_flags |= SA_RESTART;
    }
    //将sa.sa_mask设置为全信号集
    sigfillset(&sa.sa_mask);

    assert(sigaction(sig,&sa,NULL) != -1);
}
//定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler() {
    m_timer_lst.tick();
    alarm(m_TIMESLOT);
}

void Utils::show_error(int connfd, const char *info) {
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

//初始化Utils中的static成员
int* Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

