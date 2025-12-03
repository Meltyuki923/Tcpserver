//
// Created by 28252 on 25-11-24.
//

#include "log.hpp"


log::log()
{
    m_count = 0;
    m_is_async = false;
}

log::~log()
{
    if (m_fp != NULL)
    {
        fclose(m_fp);
    }
}

bool log::init(const char *file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size) {
    m_close_log = close_log;
    //如果max_queue_size >= 1，则为异步写
    if(max_queue_size >= 1){
        m_is_async = true;
        m_log_queue = new block_queue<std::string>(max_queue_size);
        //创建线程
        pthread_t tid;
        //flush_log_thread为回调函数,这里表示创建线程异步写日志
        pthread_create(&tid,NULL,flush_log_thread,NULL);
    }
    //输出内容的长度
    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf,'\0',m_log_buf_size);
    m_split_lines = split_lines;

    time_t t = time(NULL);
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

// 1. 查找file_name中最后一个'/'的位置，返回指向该字符的指针；未找到则返回NULL
    const char *p = strrchr(file_name, '/');
// 2. 初始化存储最终日志名的数组，避免野值和字符串结束符问题
    char log_full_name[512] = {0};

// 3. 分支1：file_name是纯文件名（无路径，如"app.log"）
    if (p == NULL)
    {
        // 拼接格式：年_月_日_纯文件名（如2025_11_25_app.log）
        snprintf(log_full_name, sizeof(log_full_name), "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }
// 4. 分支2：file_name带路径（如"/var/log/app.log"）
    else
    {
        // 提取最后一个'/'后的纯文件名，存入log_name（如"app.log"）
        strcpy(log_name, p + 1);
        // 提取最后一个'/'前的目录路径（包含'/'），存入dir_name（如"/var/log/"）
        strncpy(dir_name, file_name, p - file_name + 1);
        // 拼接格式：目录路径+年_月_日_纯文件名（如/var/log/2025_11_25_app.log）
        snprintf(log_full_name, sizeof(log_full_name), "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }
    m_today = my_tm.tm_mday;

    m_fp = fopen(log_full_name, "a");
    if (m_fp == NULL)
    {
        return false;
    }
    return true;
}

void log::write_log(int level, const char *format, ...) {
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0};
    switch (level) {
        case 0:{
            strcpy(s,"[debug]:");
            break;
        }
        case 1:{
            strcpy(s,"[info]:");
            break;
        }
        case 2:{
            strcpy(s,"[warn]:");
            break;
        }
        case 3:{
            strcpy(s,"[error]:");
            break;
        }
        default:{
            strcpy(s,"[info]:");
            break;
        }
    }
    m_mutex.lock();
    m_count++;
    if(m_today != my_tm.tm_mday || m_count % m_split_lines == 0){
        char new_log[512];//日志名
        fflush(m_fp);
        fclose(m_fp);
        char tail[16];//日期后缀
        //生成日期后缀
        snprintf(tail,16,"%d_%02d_%02d_",1900 + my_tm.tm_year,my_tm.tm_mon + 1,my_tm.tm_mday);
        //不是今天
        if(m_today != my_tm.tm_mday){
            //新文件名：目录+日期后缀+原文件名（如/var/log/2025_11_25_app.log）
            snprintf(new_log, sizeof(new_log),"%s%s%s",dir_name,log_name,tail);
            m_today = my_tm.tm_mday;
            //重置行数:因为换了个日志文件
            m_count = 0;
        }
        //超出最大行数
        else{
            //新文件名：目录+日期后缀+原文件名.分割序号（如/var/log/2025_11_25_app.log.1）
            snprintf(new_log,sizeof(new_log),"%s%s%s.%lld",dir_name,log_name,tail,m_count / m_split_lines);
        }
        m_fp = fopen(new_log,"a");
    }
    m_mutex.unlock();

    va_list valst;
    va_start(valst,format);
    std::string log_str;

    m_mutex.lock();
    //拼接日志内容First ：时间戳 + 级别标签 ( 2025-11-25 20:46:21.114514 [info] )
    int n = snprintf(m_buf,48,"%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900,my_tm.tm_mon + 1,my_tm.tm_mday,
                     my_tm.tm_hour,my_tm.tm_min,my_tm.tm_sec,
                     now.tv_usec,s);
    //拼接日志内容Second ：用户传入内容（具体的日志信息）
    int m = vsnprintf(m_buf + n,m_log_buf_size - n - 1,format,valst);
    m_buf[m+n] = '\n'; //添加换行
    m_buf[m+n+1] = '\0'; //添加此次日志的结尾
    log_str = m_buf; //转换成string 方便后续操作:m_log_queue的数据类型为string

    m_mutex.unlock();
    //异步且队列没满
    if(m_is_async && !m_log_queue->full()){
        m_log_queue->push(log_str);
    }
    //同步或队列满
    else{
        m_mutex.lock();
        fputs(log_str.c_str(),m_fp);
        m_mutex.unlock();
    }
}

void log::flush(void)
{
    m_mutex.lock();
    //强制刷新写入流缓冲区
    fflush(m_fp);
    m_mutex.unlock();
}