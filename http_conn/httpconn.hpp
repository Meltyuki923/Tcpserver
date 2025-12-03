//
// Created by 28252 on 25-11-12.
//

#ifndef TCPSERVER_HTTPCONN_HPP
#define TCPSERVER_HTTPCONN_HPP
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
#include <map>
#include <string>

#include "../lock/lockers.h"
#include "../cgi_mysql/sql_connection_pool.hpp"
#include "../timer/lst_timer.hpp"
#include "../log/log.hpp"


class httpconn {
public:
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;
    //http请求方法，我们仅支持GET，POST
    enum METHOD {GET = 0,POST,HEAD,PUT,DELETE,TRACE,OPTIONS,CONNECT,PATCH};
    //解析客户请求时，主状态机所处的状态
    enum CHECK_STATE {CHECK_STATE_REQUESTLINE = 0,
                      CHECK_STATE_HEADER,
                      CHECK_STATE_CONTENT};
    //服务器处理HTTP请求的可能结果
    enum HTTP_CODE {NO_REQUEST,GET_REQUEST,BAD_REQUEST,NO_RESOURCE,FORBIDDEN_REQUEST,FILE_REQUEST,INTERNAL_ERROR,CLOSED_CONNECTION};
    //行的读取状态
    enum LINE_STATUS {LINE_OK = 0,LINE_BAD,LINE_OPEN };
public:
    httpconn() {};
    ~httpconn() {};

public:
    //初始化新连接
    void init(int sockfd,const sockaddr_in& addr, char *root, int TRIGMode,
              int close_log, std::string user, std::string passwd, std::string sqlname);
    //关闭连接
    void close_conn(bool real_close = true);
    //处理客户请求
    void process();
    //非阻塞读操作
    bool read_once();
    //非阻塞写操作
    bool write();

    sockaddr_in *get_address()
    {
        return &m_address;
    }
    //本质是将数据库的内容存入到服务器上
    void initmysql_result(sql_connection_pool *connPool);
    int timer_flag;
    int improv;

private:
    //初始化连接
    void init();
    //解析http请求
    HTTP_CODE process_read();
    //填充HTTP应答
    bool process_write(HTTP_CODE ret);

    //下面一组函数被process_read调用来分析http请求
    HTTP_CODE parse_request_line(char* text);
    HTTP_CODE parse_request_headers(char* text);
    HTTP_CODE parse_request_content(char* text);
    HTTP_CODE do_request();
    char* get_line() {return m_read_buf + m_start_line;}
    LINE_STATUS parse_line();

    //下面一组函数被process_write调用来填充HTTP应答
    void unmap();
    bool add_response(const char* format, ...);
    bool add_content(const char* content);
    bool add_status_line(int status, const char* title);
    bool add_headers(int content_length);
    bool add_content_length(int content_length);
    bool add_content_type();
    bool add_linger();
    bool add_blank_line();


public:
    //所有socket上事件被注册到同一个epoll内核事件表中，所以将epoll文件描述符设置为静态的
    static int m_epollfd;
    //统计用户数量
    static int m_user_count;
    //数据库
    MYSQL* mysql;
    int m_state; //读为0，写为1
private:
    //该http连接的socket和对方的socket地址
    int m_sockfd;
    sockaddr_in m_address;

    //读缓冲区
    char m_read_buf[READ_BUFFER_SIZE];
    //标识读缓冲区中已经读入的客户数据的最后一个字节的下一个位置
    int m_read_idx;
    //当前正在分析的字符在读缓冲区中的位置
    int m_check_idx;
    //当前正在解析的行的起始位置
    int m_start_line;
    //写缓冲区
    char m_write_buf[WRITE_BUFFER_SIZE];
    //写缓冲区中待发送的字节数
    int m_write_idx;

    //主状态机当前所处的状态
    CHECK_STATE m_check_state;
    //请求方法
    METHOD m_method;

    //客户端请求的目标文件的完整路径，其内容等于doc_root + m_url,doc_root 是网站根目录
    char m_real_file[FILENAME_LEN];
    //客户端请求的目标文件的文件名
    char *m_url;
    //http协议版本号，仅支持http/1.1
    char* m_version;
    //主机名
    char* m_host;
    //http请求的消息体的长度
    int m_content_length;
    //http请求是否要保持连接
    bool m_linger;

    //客户请求的目标文件被mmap到内存中的起始位置
    char* m_file_address;
    //目标文件的状态，通过它我们可以判断文件是否存在、是否为目录，是否可读，并获取文件大小等
    struct stat m_file_stat;
    //我们将采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量
    struct iovec m_iv[2];
    int m_iv_count;
    //是否启用Post
    int cgi;
    //存储请求头数据(contnet)
    char* m_string;
    //剩余的要发送的字节数
    int bytes_to_send;
    //已经发送的字节数
    int bytes_have_send;
    char *doc_root;

    std::map<std::string, std::string> m_users;
    int m_TRIGMode;
    int m_close_log;

    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
};


#endif //TCPSERVER_HTTPCONN_HPP
