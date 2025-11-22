//
// Created by 28252 on 25-11-12.
//

#include "httpconn.hpp"

const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";


int setnonblocking(int fd){
    int old_option = fcntl(fd,F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd,F_SETFL,new_option);
    return old_option;
}

void addfd(int epfd, int fd, bool one_shot){
    epoll_event event;
    event.data.fd = fd;
#ifdef LT
    event.events = EPOLLIN | EPOLLRDHUP;
#endif
#ifdef ET
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
#endif
    if(one_shot){
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epfd,EPOLL_CTL_ADD,fd,&event);
    setnonblocking(fd);
}

void removefd(int epfd, int fd){
    epoll_ctl(epfd,EPOLL_CTL_DEL,fd,0);
    close(fd);
}
//重置EPOLLONESHOT
void modfd(int epfd, int fd, int ev){
    epoll_event event;
    event.data.fd = fd;
#ifdef LT
    event.events = ev | EPOLLIN | EPOLLRDHUP;
#endif
#ifdef ET
    event.events = ev | EPOLLIN | EPOLLET | EPOLLRDHUP;
#endif
    epoll_ctl(epfd,EPOLL_CTL_MOD,fd,&event);
}

//初始化用户数量和epfd
int httpconn::m_user_count = 0;
int httpconn::m_epollfd = -1;


void httpconn::close_conn(bool real_close) {
    //sockfd = -1 是指socket创建失败
    if(real_close && m_sockfd != -1){
        removefd(m_epollfd,m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

//socket初始化
void httpconn::init(int sockfd, const sockaddr_in &addr) {
    m_sockfd = sockfd;
    m_address = addr;
    addfd(m_epollfd,sockfd, true);
    m_user_count++;
    init();
}

void httpconn::init() {
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;

    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = '\0';
    m_check_idx = 0;
    m_read_idx = 0;
    m_read_idx = 0;
    memset(m_read_buf,'\0',READ_BUFFER_SIZE);
    memset(m_write_buf,'\0',WRITE_BUFFER_SIZE);
    memset(m_read_buf,'\0',FILENAME_LEN);
}



/*
 * HTTP处理报文的核心逻辑:处理读写
 */
void httpconn::process() {
    HTTP_CODE read_ret = process_read();
    if(read_ret == NO_REQUEST){
        //注册监听读
        modfd(m_epollfd,m_sockfd,EPOLLIN);
        return;
    }

    bool write_ret = process_write(read_ret);
    if(!write_ret){
        close_conn();
    }
    //注册监听写事件
    modfd(m_epollfd,m_sockfd,EPOLLOUT);
}




//循环读取客户数据，直到无数据可读或对方关闭连接
//一次读完所有数据，符合ET模式“只通知一次就绪”
bool httpconn::read() {
    if(m_read_idx >= READ_BUFFER_SIZE){
        return false;
    }
    int byte_read = 0;
    while(true){
        byte_read = recv(m_sockfd,m_read_buf+m_read_idx,READ_BUFFER_SIZE - m_read_idx,0);
        //表示读取错误
        if(byte_read == -1){
            //如果错误为EAGAIN或EWOULDBLOCK，表示成功读取完所有数据，跳出循环
            if(errno == EAGAIN || errno == EWOULDBLOCK) break;
            //表示发生了其他错误，返回false
            return false;
        }
        //表示客户端主动关闭连接
        else if(byte_read == 0){
            return false;
        }
        m_read_idx += byte_read;

        //如果byte_read > 0 表示正常读取，此时继续循环
    }
    return true;
}



//主状态机
httpconn::HTTP_CODE httpconn::process_read() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;
    //
    while((line_status == LINE_OK && m_check_state == CHECK_STATE_CONTENT) || (line_status = parse_line()) == LINE_OK){
        text = get_line();
        //m_start_line是每一个数据行在m_read_buf中的起始位置
        //m_check_idx表示从状态机在m_read_buf中读取的当前位置
        m_start_line = m_check_idx;
        switch (m_check_state) {
            case CHECK_STATE_REQUESTLINE:
            {
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST) return BAD_REQUEST;
                break;
            }
            case CHECK_STATE_HEADER:
            {
                ret = parse_request_headers(text);
                if(ret == BAD_REQUEST) return BAD_REQUEST;
                else if(ret = GET_REQUEST){
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT:
            {
                ret = parse_request_content(text);
                if(ret == BAD_REQUEST) return BAD_REQUEST;
                else if(ret == GET_REQUEST){
                    return do_request();
                }
                //在POST的情况下，报文最后正常情况下是没有行结束符（\r\n）的，此时line_status为LINE_OPEN。
                // 如果 POST 消息体包含 \r\n 字符（比如表单数据、JSON 数据中恰好有换行符）,此时line_status无法变成LINE_OPEN会导致死循环
                //所以这里需要显示的设置为LINE_OPEN
                line_status = LINE_OPEN;
                break;
            }
            default:
                return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

//从状态机
httpconn::LINE_STATUS httpconn::parse_line() {
    char temp;
    for(;m_check_idx < m_read_idx; m_check_idx++){
        temp = m_read_buf[m_check_idx];//获取当前字符
        if(temp == '\r')
        {
            if(m_read_buf[m_check_idx +1] == '\n'){
                m_read_buf[m_check_idx++] = '\0';
                m_read_buf[m_check_idx++] = '\0';
                return LINE_STATUS::LINE_OK;
            }
            //到达buf末尾
            else if((m_check_idx + 1) == m_read_idx){
                return LINE_STATUS::LINE_OPEN;
            }
            return LINE_STATUS::LINE_BAD;
        }
        else if(temp == '\n')
        {   //查询前一个字符是否为'\r'
            if((m_read_buf[m_check_idx - 1] == '\r') && (m_check_idx > 1)){
                m_read_buf[m_check_idx - 1] == '\0';
                m_read_buf[m_check_idx++] == '\0';
                return LINE_STATUS::LINE_OK;
            }
            return LINE_STATUS::LINE_BAD;
        }
        //没找到'\r'或'\n'
        return LINE_STATUS::LINE_OPEN;
    }
}


/*
 * 以下为主状态机用来解析请求行，请求头，内容的三个解析函数
 * parse_request_line
 * parse_request_headers
 * parse_request_content
 */

//解析http请求行，获得请求方法，目标url及http版本号
httpconn::HTTP_CODE httpconn::parse_request_line(char *text)
{
    m_url = strpbrk(text, " \t");
    if (!m_url)
    {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';
    char *method = text;
    if (strcasecmp(method, "GET") == 0)
        m_method = GET;
    else if (strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
    }
    else
        return BAD_REQUEST;
    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
        return BAD_REQUEST;
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;
    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }

    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;
    //当url为/时，显示判断界面
    if (strlen(m_url) == 1)
        strcat(m_url, "judge.html");
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}
//解析请求头：判断是什么请求（GET OR POST），提取Connection、Content—length，Host信息，其他信息不提取
httpconn::HTTP_CODE httpconn::parse_request_headers(char* text) {
    if (text[0] == '\0')
    {
        if (m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
        }
    }
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else
    {
        printf("oop!unknow header: %s", text);
    }
    return NO_REQUEST;
}
//解析请求内容：
httpconn::HTTP_CODE httpconn::parse_request_content(char* text) {
    if (m_read_idx >= (m_content_length + m_check_idx))
    {
        text[m_content_length] = '\0';
        //POST请求中最后为输入的用户名和密码
//        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

const char* doc_root = "/home/meltyuki923/github/ini_tinywebserver/root";


//根据请求执行相应操作：寻找请求文件，并mmap到内存中
httpconn::HTTP_CODE httpconn::do_request() {
    strcpy(m_real_file,doc_root);
    int len = strlen(doc_root);

    //寻找m_rul中请求文件的位置('/')
    const char *p = strrchr(m_url,'/');

    //实现登录和注册校验

    //请求资源为0
    if(*(p+1) == '0'){
        char* dst = (char*) malloc(sizeof (char) * 200);
        strcpy(dst,"/register.html");
        //拼接
        strncpy(m_real_file + len,dst, strlen(dst));
        free(dst);
    }
    //为1
    else if(*(p+1) == '1'){
        char* dst = (char*) malloc(sizeof (char) * 200);
        strcpy(dst,"/log.html");
        strncpy(m_real_file + len,dst, strlen(dst));
        free(dst);
    }
    //都不是,直接拼接url，这里的情况是welcome界面，请求服务器上的一个图片
    else{
        strncpy(m_real_file + len,m_url,FILENAME_LEN - 1);
    }
    //没有此文件
    if(stat(m_real_file,&m_file_stat) < 0){
        return NO_RESOURCE;
    }
    //判断权限：是否可读，如不可返回FORBIDDEN_REQUEST
    if(!(m_file_stat.st_mode&S_IROTH)){
        return FORBIDDEN_REQUEST;
    }
    //判断文件类型：如果为目录，返回BAD_REQUEST表示请求报文有误
    if(S_ISDIR(m_file_stat.st_mode)){
        return BAD_REQUEST;
    }
    //以只读打开
    int fd = open(m_real_file,O_RDONLY);
    //mmap将文件映射至内存
    m_file_address=(char*)mmap(0,m_file_stat.st_size,PROT_READ,MAP_PRIVATE,fd,0);
    //关闭fd避免文件描述符的浪费
    close(fd);
    //文件存在
    return FILE_REQUEST;
}
/*
 * 以下为写入m_write_buf的函数，包括：
 * 中间件:add_response()
 * 添加内容（通过调用中间件来实现）
 * 添加具体的内容process_write()
 */

//负责在m_write_buf中写入数据：更新m_write_idx指针和m_write_buf缓冲区
bool httpconn::add_response(const char* format, ...) {
    if(m_write_idx > WRITE_BUFFER_SIZE){
        return false;
    }

    va_list arg_list;
    va_start(arg_list,format);
    //写入m_write_buf 返回 写入的长度
    int len = vsnprintf(m_write_buf + m_write_idx,WRITE_BUFFER_SIZE - m_write_idx - 1,format,arg_list);
    if((len + m_write_idx) >= WRITE_BUFFER_SIZE){
        //清空可变参数列表
        va_end(arg_list);
        return false;
    }
    //更新m_write_idx
    m_write_idx += len;
    va_end(arg_list);

    return true;
}

//添加状态行
bool httpconn::add_status_line(int status, const char* title) {
    return add_response("%s %d %s\r\n","HTTP/1.1",status,title);
}
//添加消息报头，具体的添加文本长度、连接状态和空行：通过调用一下三个函数来实现
bool httpconn::add_headers(int content_length) {
    add_content_length(content_length);
    add_linger();
    add_blank_line();
}
//添加Content-Length，表示响应报文的长度
bool httpconn::add_content_length(int content_length) {
    return add_response("Content-Length:%d\r\n",content_length);
}
//添加文本类型，这里是html
bool httpconn::add_content_type() {
    return add_response("Content-Type:%s\r\n","text/html");
}
//添加连接状态，通知浏览器端是保持连接还是关闭
bool httpconn::add_linger() {
    return add_response("Connection:%s\r\n",(m_linger==true)?"keep-alive":"close");
}
//添加空行
bool httpconn::add_blank_line() {
    return add_response("%s","\r\n");
}
//添加文本content
bool httpconn::add_content(const char* content) {
    return add_response("%s\r\n",content);
}


bool httpconn::process_write(httpconn::HTTP_CODE ret) {
    switch (ret) {
        case INTERNAL_ERROR:
        {
            add_status_line(500,error_500_title);
            add_headers(strlen(error_500_form));
            if(!add_content(error_500_form)){
                return false;
            }
            break;
        }
        case BAD_REQUEST:
        {
            add_status_line(404,error_404_title);
            add_headers(strlen(error_500_form));
            if(!add_content(error_404_form)){
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST:
        {
            add_status_line(403,error_403_title);
            add_headers(strlen(error_403_form));
            if(!add_content(error_404_form)){
                return false;
            }
            break;
        }
        case FILE_REQUEST:
        {
            add_status_line(200,ok_200_title);

            //m_file_stat被初始化过吗？
            //如果请求的文件不为空
            if(m_file_stat.st_size != 0){
                add_headers(m_file_stat.st_size);
                //指向m_write_buf
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                //指向mmap的地址
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                //需要发送的总字节数
                bytes_to_send = m_write_idx + m_file_stat.st_size;
                m_iv_count = 2;
                return true;
            }
            //为空
            else{
                //如果请求的资源大小为0，则返回空白html文件
                const char* ok_string="<html><body></body></html>";
                add_headers(strlen(ok_string));
                if(!add_content(ok_string)){
                    return false;
                }
            }
        }
        default:
            return false;
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[1].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

//服务器把报文发送给浏览器（客户端）
bool httpconn::write() {
    int temp = 0;
    int newadd = 0;

    //待发送数据为0，一般不发生
    if(bytes_to_send == 0){
        modfd(m_epollfd,m_sockfd,EPOLLIN);
        init();
        return true;
    }

    while(1){
        temp = writev(m_sockfd,m_iv,m_iv_count);

        //正常发送
        if(temp > 0){
            //已发送字节
            bytes_have_send += temp;
            //偏移结构体iovec的指针,大于等于0
            newadd = bytes_have_send - m_write_idx;
        }
        //发送失败
        if(temp <= -1){
            //判断缓冲区是否满
            if(errno == EAGAIN){
                //第一个iovec头部信息的数据已发送完，发送第二个iovec数据
                if(bytes_have_send >= m_iv[0].iov_len){
                    //不再发送头部数据
                    m_iv[0].iov_len = 0;
                    m_iv[1].iov_base = m_file_address + newadd;
                    m_iv[1].iov_len = bytes_to_send;
                }
                //没发完，继续发送第一个iovec头部信息数据
                else{
                    m_iv[0].iov_base = m_write_buf + bytes_to_send;
                    m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
                }
                modfd(m_epollfd,m_sockfd,EPOLLIN);
                return true;
            }
            unmap();
            return false;
        }
        //发送完毕
        if(bytes_to_send <= 0){
            unmap();
            //切换为EPOLLIN，等待被发送新的请求。如果仍为EPOLLOUT会持续出发写事件，浪费cpu资源
            modfd(m_epollfd,m_sockfd,EPOLLIN);
            if(m_linger){
                init();
                //向上层函数返回「发送成功且连接保留」的信号
                return true;
            }
            else{
                //向上层函数返回「发送成功但需关闭连接」的信号
                return false;
            }
        }
    }
}



