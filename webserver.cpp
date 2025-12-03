//
// Created by 28252 on 25-11-28.
//

#include "webserver.hpp"

webserver::webserver() {
    //httpconn数组,管理全局http链接
    users = new httpconn[MAX_FD];

    char server_path[200];
    getcwd(server_path,200);
    const char root[] = "/root";
    m_root = (char*) malloc(strlen(server_path) +strlen(root) + 1);
    if (m_root) {
        strcpy(m_root, server_path);
        strcat(m_root, root); // 改为 strcat，避免覆盖
    }

    //client_data数组，管理全局定时器?
    users_timer = new client_data[MAX_FD];
}

webserver::~webserver() {
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete m_pool;
    free(m_root);
    m_root = nullptr;
}

void webserver::init(int port, std::string user, std::string passWord, std::string databaseName, int log_write,
                     int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model) {
    m_port = port;
    m_user = user;
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_log_write = log_write;
    m_OPT_LINGER = opt_linger;
    m_TRIGMode = trigmode;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_close_log = close_log;
    m_actormodel = actor_model;
}

void webserver::sql_pool() {
    //初始化数据库连接池
    m_connPool = sql_connection_pool::GetInstance();
    m_connPool->init("localhost",m_user,m_passWord,m_databaseName,3306,m_sql_num,m_close_log);
    //初始化数据库读取表
    users->initmysql_result(m_connPool);
}

void webserver::thread_pool() {
    m_pool = new threadpool<httpconn>(m_actormodel,m_connPool,m_thread_num);
}

void webserver::log_write() {
    if (0 == m_close_log)
    {
        //初始化日志
        if (1 == m_log_write)
            log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        else
            log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
    }
}

void webserver::trig_mode() {
    //LT + LT
    if(m_TRIGMode == 0){
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    //LT + ET
    else if (1 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    //ET + LT
    else if (2 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    //ET + ET
    else if (3 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

void webserver::eventListen() {
    m_listenfd = socket(PF_INET,SOCK_STREAM,0);
    assert(m_listenfd >= 0);
    //优雅关闭
    if(m_OPT_LINGER == 0){
        struct linger tmp = {0,1};
        setsockopt(m_listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));
    }
    else if(m_OPT_LINGER == 1){
        struct linger tmp = {1,1};
        setsockopt(m_listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));
    }
    //初始化一个 IPv4 套接字地址结构
    //服务器的套接字将监听本机所有网卡的 IPv4 地址；
    //监听的端口号为 m_port
    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);
    //完成套接字选项设置、地址绑定和监听客户端连接
    int flag = 1;
    //为套接字 m_listenfd 设置套接字层的可重用地址选项
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    ret = bind(m_listenfd, (struct sockaddr*)&address, sizeof(address));
    if (ret == -1) {
        // 打印错误码和描述（需要包含 <string.h> 头文件）
        perror("bind failed");  // 直接输出错误描述，如 "bind failed: Address already in use"
        printf("errno: %d, port: %d\n", errno, m_port);  // 输出错误码和绑定的端口
        exit(EXIT_FAILURE);  // 或根据需求处理，不要直接assert
    }
    //将套接字 m_listenfd 从主动套接字转换为被动套接字，并开始监听客户端的连接请求
    ret = listen(m_listenfd,128);
    assert(ret >= 0);

    utils.init(TIMESLOT);

    //epoll创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    utils.addfd(m_epollfd,m_listenfd, false,m_LISTENTrigmode);
    //将创建的 epoll 实例文件描述符 m_epollfd 赋值给 httpconn 类的静态成员变量 m_epollfd。
    //方便后续在处理客户端连接时，直接通过该静态成员向 epoll 中添加 / 修改 / 删除客户端套接字的事件（无需每次传递 epollfd）
    httpconn::m_epollfd = m_epollfd;

    //创建一对相互连接的 UNIX 域套接字
    ret = socketpair(PF_UNIX,SOCK_STREAM,0,m_pipefd);
    assert(ret != -1);
    //写端 m_pipefd[1] 不注册到 epoll，仅用于主动写入数据触发读端的事件
    //假设写端是阻塞模式，当读端的接收缓冲区被占满（如主线程未及时读取数据），此时调用 write(m_pipefd[1], data, len) 会阻塞当前线程
    utils.setnonblocking(m_pipefd[1]);
    //使用LT,仅注册读端 m_pipefd[0] 到 epoll，监听其 EPOLLIN（读事件）
    utils.addfd(m_epollfd,m_pipefd[0], false,0);

    //添加信号
    utils.addsig(SIGPIPE, SIG_IGN);
    utils.addsig(SIGALRM, utils.sig_handler, false);
    utils.addsig(SIGTERM, utils.sig_handler, false);

    //调用 alarm(TIMESLOT) 后，内核会为当前进程设置一个定时器，倒计时 TIMESLOT 秒。
    //alarm 是一次性定时器：定时器触发一次后就会失效
    alarm(TIMESLOT);

    //工具类,信号和描述符基础操作
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}

//初始化 HTTP 连接对象、绑定定时器
void webserver::timer(int connfd, struct sockaddr_in client_address) {
    users[connfd].init(connfd,client_address,m_root,m_TRIGMode,m_close_log,m_user,m_passWord,m_databaseName);

    //初始化client_data数据
    //创建定时器，设置回调函数和超时时间，绑定客户端数据，将定时器添加到链表中
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    util_timer *timer = new util_timer;
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func;
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    users_timer[connfd].timer = timer;
    utils.m_timer_lst.add_timer(timer);
}

//若有数据传输，则将定时器往后延迟3个单位
//并对新的定时器在链表上的位置进行调整
void webserver::adjust_timer(util_timer *timer) {
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);
    LOG_INFO("%s", "adjust timer once");
}

void webserver::deal_timer(util_timer *timer, int sockfd) {
    //在epoll事件表上取消监听 和 关闭fd在cb_func中执行
    timer->cb_func(&users_timer[sockfd]);
    if (timer)
    {
        utils.m_timer_lst.del_timer(timer);
    }

    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}


bool webserver::dealclientdata() {
    struct sockaddr_in client_address;
    socklen_t client_addrlen = sizeof(client_address);
    //LT
    if(m_LISTENTrigmode == 0){
        int connfd = accept(m_listenfd,(struct sockaddr*)&client_address,&client_addrlen);
        if(connfd < 0){
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if(httpconn::m_user_count >= MAX_FD){
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        timer(connfd,client_address);
    }
    //ET
    else{
        while(true){
            int connfd = accept(m_listenfd,(struct sockaddr*)&client_address,&client_addrlen);
            if(connfd < 0){
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if(httpconn::m_user_count >= MAX_FD){
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd,client_address);
        }
        return false;
    }
    return true;
}

bool webserver::dealwithsignal(bool &timeout, bool &stop_server) {
    int ret = 0;
    int sig;
    char signals[1024]; //字符数组，作为缓冲区存储从管道中读取的信号数据（每个字节对应一个信号）。
    ret = recv(m_pipefd[0],signals,sizeof(signals),0);
    if(ret == -1 || ret == 0){
        return false;
    }
    else{
        for(int i = 0;i < ret;i++){
            switch (signals[i]) {
                case SIGALRM:
                {
                    timeout = true;
                    break;
                }
                case SIGTERM:
                {
                    stop_server = true;
                    break;
                }
            }
        }
    }
    return true;
}

void webserver::dealwithread(int sockfd) {
    util_timer* timer = users_timer[sockfd].timer;
    //Reactor：主线程只需把任务塞进threadpool，工作线程需读数据，做逻辑
    if(m_actormodel == 1){
        if(timer != nullptr){
            adjust_timer(timer);
        }
        //state为0 表示读事件
        m_pool->append(users + sockfd,0);
        while(true){
            if(users[sockfd].improv == 1){
                if(users[sockfd].timer_flag == 1){
                    deal_timer(timer,sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    //Proactor：主线程需要读取数据然后再把任务塞进threadpool，工作线程只需做逻辑
    else{
        if(users[sockfd].read_once()){
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            m_pool->append(users + sockfd,0);

            if(timer != nullptr){
                adjust_timer(timer);
            }
        }
        else{
            deal_timer(timer,sockfd);
        }
    }
}

void webserver::dealwithwrite(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;
    //reactor
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer);
        }

        m_pool->append(users + sockfd, 1);

        while (true)
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        //proactor
        if (users[sockfd].write())
        {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}

void webserver::eventLoop() {
    bool timeout = false;
    bool stop_server = false;

    while(!stop_server){
        int num = epoll_wait(m_epollfd,events,MAX_EVENT_NUMBER,-1);
        if(num < 0 && errno != EINTR){
            LOG_ERROR("%s", "epoll failure");
            break;
        }
        for(int i = 0;i < num;i++){
            int sockfd = events[i].data.fd;
            // 分支1：处理新客户端连接（监听套接字事件）
            if(sockfd == m_listenfd){
                bool flag = dealclientdata();
                if(!flag){
                    continue;
                }
            }
            // 分支2：处理客户端连接的异常事件（断开/错误）
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            // 分支3：处理信号通知事件（管道读端事件
            else if((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN)){
                bool flag = dealwithsignal(timeout,stop_server);
                if(!flag){
                    LOG_ERROR("%s", "dealwithsignal failure");
                }
            }
            // 分支4：处理客户端连接的读事件
            else if (events[i].events & EPOLLIN)
            {
                dealwithread(sockfd);
            }
            // 分支5：处理客户端连接的写事件
            else if (events[i].events & EPOLLOUT)
            {
                dealwithwrite(sockfd);
            }
        }
        if(timeout){
            utils.timer_handler();
            LOG_INFO("%s", "timer tick");
            timeout = false;
        }
    }
}












