#ifndef _MY_HTTP_CONN_H_
#define _MY_HTTP_CONN_H_

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
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
#include "my_locker.h"
#include "my_parse.h"

class my_httpconn
{
    friend class my_parse;
public:
    my_httpconn() { }   
    ~my_httpconn() { delete m_parse; } 

    /** 初始化新的连接 **/
    void init(int sockfd, const sockaddr_in& addr);
    /** 关闭连接 **/
    void close_conn(bool real_close = true);
    /** 处理客户请求 **/
    void process();

    /** 非阻塞读操作 **/
    bool read();
    /** 非阻塞写操作 **/
    bool write();


public: 
    /** 所有的socket上的事件都被注册到同一个epoll内核事件表中，所以将epollfd设置为静态的 **/
    /** 所有的请求连接都会分配一个类的实例，而epollfd显然不希望与每个对象相关，只需要与类本身即可 **/
    /** 所有的连接的事件都被注册到同一个epoll事件内核表，而不是每个连接都会有自己的epoll事件内核表**/
    static int m_epollfd;
    /** 统计用户数量 **/
    static int m_user_count;

private:
    /** 与http服务器连接的对方的sockfd和地址 **/
    int                         m_sockfd;
    sockaddr_in                 m_address;
    /** 用于解析http头部信息 **/
    my_parse*                   m_parse;
};


#endif 