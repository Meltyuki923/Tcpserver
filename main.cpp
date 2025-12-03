#include "config.hpp"

int main(int argc, char*argv[]){
    //数据库信息
    std::string user = "root";
    std::string passwd = "root";
    std::string databasename = "meltdb";

    //解析命令行参数
    config conf;
    conf.parse_arg(argc,argv);

    webserver server;
    server.init(conf.PORT,user,passwd,databasename,conf.LOGWrite,
                conf.OPT_LINGER,conf.TRIGMode,conf.sql_num,conf.thread_num,conf.close_log,conf.actor_model);

    //启动触发模式
    server.trig_mode();
    //启动日志写
    server.log_write();
    //启动数据库连接池
    server.sql_pool();
    //启动线程池
    server.thread_pool();
    //启动监听
    server.eventListen();
    //启动事件循环
    server.eventLoop();

    return 0;
}