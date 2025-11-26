//
// Created by 28252 on 25-11-26.
//

#include "sql_connection_pool.hpp"


sql_connection_pool::sql_connection_pool() {
    m_CurConn = 0;
    m_FreeConn = 0;
}

sql_connection_pool *sql_connection_pool::GetInstance() {
    static sql_connection_pool conPool;
    return &conPool;
}

void sql_connection_pool::init(std::string url, std::string User, std::string PassWord, std::string DataBaseName, int Port,int MaxConn, int close_log){
    m_url = url;
    m_User = User;
    m_PassWord = PassWord;
    m_DatabaseName = DataBaseName;
    m_close_log = close_log;
    for(int i = 0;i<MaxConn;i++){
        MYSQL* con = nullptr;
        con = mysql_init(con);
        if(con == nullptr){
            LOG_ERROR("Mysql Error");
            exit(-1);
        }
        con = mysql_real_connect(con,m_url.c_str(),m_User.c_str(),m_PassWord.c_str(),m_DatabaseName.c_str(),Port,NULL,0);
        if(con == nullptr){
            LOG_ERROR("Mysql Error");
            exit(-1);
        }
        connList.push_back(con);
        m_FreeConn++;
    }
    reserve = sem(m_FreeConn);
    m_MaxConn = m_FreeConn;
}

MYSQL *sql_connection_pool::GetConnection() {
    if(connList.size() == 0){
        return nullptr;
    }
    MYSQL* con = nullptr;

    reserve.wait();

    lock.lock();
    con = connList.front();
    connList.pop_front();
    m_FreeConn--;
    m_CurConn++;
    lock.unlock();

    return con;
}

bool sql_connection_pool::ReleaseConnection(MYSQL *conn) {
    if(conn == nullptr) return false;

    lock.lock();
    connList.push_back(conn);
    m_FreeConn++;
    m_CurConn--;
    lock.unlock();

    reserve.post();
    return true;
}

int sql_connection_pool::GetFreeConn() {
    return this->m_FreeConn;
}

void sql_connection_pool::DestroyPool() {
    lock.lock();

    if(connList.size() > 0){
        for(auto it = connList.begin();it != connList.end(); it++){
            //这里仅仅是释放了连接的内存，connList中的野指针仍然存在
            MYSQL *con = *it;
            mysql_close(con);
        }
        m_CurConn = 0;
        m_FreeConn = 0;
        //通过clear释放所有野指针。
        connList.clear();
    }
    lock.unlock();
}


sql_connection_pool::~sql_connection_pool()
{
    DestroyPool();
}



//其中数据库连接本身是指针类型，所以参数需要通过双指针才能对其进行修改。
connectionRAII::connectionRAII(MYSQL **con, sql_connection_pool *connpool) {
    *con = connpool->GetConnection();

    conRAII = *con;
    poolRAII = connpool;
}

connectionRAII::~connectionRAII() {
    poolRAII->ReleaseConnection(conRAII);
}
