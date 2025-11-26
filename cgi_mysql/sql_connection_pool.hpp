//
// Created by 28252 on 25-11-26.
//

#ifndef TCPSERVER_SQL_CONNECTION_POOL_HPP
#define TCPSERVER_SQL_CONNECTION_POOL_HPP

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include "../lock/lockers.h"
#include "../log/log.hpp"
#include <string>
#include <stdlib.h>
#include <pthread.h>
class sql_connection_pool {

public:
    //获取数据库连接
    MYSQL* GetConnection();
    //释放一个连接
    bool ReleaseConnection(MYSQL* conn);
    //获取当前的m_FreeConn的值
    int GetFreeConn();
    //销毁池
    void DestroyPool();

    //单例模式
    static sql_connection_pool* GetInstance();

    void init(std::string url, std::string User, std::string PassWord, std::string DataBaseName, int Port, int MaxConn, int close_log);

private:
    sql_connection_pool();
    ~sql_connection_pool();

    int m_MaxConn;  //最大连接数
    int m_CurConn;  //当前已使用的连接数
    int m_FreeConn; //当前空闲的连接数
    mutex lock;
    std::list<MYSQL *> connList; //连接池
    sem reserve;

public:
    std::string m_url;			 //主机地址
    std::string m_Port;		 //数据库端口号
    std::string m_User;		 //登陆数据库用户名
    std::string m_PassWord;	 //登陆数据库密码
    std::string m_DatabaseName; //使用数据库名
    int m_close_log;	//日志开关
};


class connectionRAII{
public:
    connectionRAII(MYSQL** con,sql_connection_pool* connpool);
    ~connectionRAII();

private:
    MYSQL* conRAII;
    sql_connection_pool* poolRAII;
};

#endif //TCPSERVER_SQL_CONNECTION_POOL_HPP
