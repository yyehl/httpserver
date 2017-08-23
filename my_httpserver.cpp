#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "my_locker.h"
#include "my_threadpool.h"
#include "my_httpconn.h"

#define MAX_FD              65536
#define MAX_EVENT_NUMBER    10000

extern int addfd(int epollfd, int fd, bool one_shot);
extern int removefd(int epollfd, int fd);

void addsig(int sig, void(handler)(int), bool restart = true)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handle = handle;  // 
    if (restart)
    {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

void show_error(int connfd, const char* info)
{
    printf("%s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int main(int argc, char* argv[])
{
    if (argc <= 2)
    {
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }
    const char* ip = argv[1];    
    int port = atoi(argv[2]);

    addsig(SIGPIPE, SIG_IGN);

    threadpool<http_conn>* pool = NULL;
    try 
    {
        pool = new threadpool<http_conn>;
    }
    catch(...)
    {
        return 1;
    }

    /** 预先为每一个可能连接的客户分配一个http_conn对象 **/
    http_conn* users = new http_conn[MAX_FD];
    assert(users);                              // 如果分配失败，users为空，则将会异常
    int user_count = 0;                         // 记录当前用户连接数量

    int listenfd  = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);                      // listenfd小于0的话说明发生了错误，发出异常
    struct linger tmp = {1, 0};                 // 作为sock选项设置的参数，用于设置优雅退出，还是强制退出
    setsockopt(listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

    int ret = 0;
    struct sockaddr_in address;                 // 设置服务器的地址
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret >= 0);

    ret = listen(listenfd, 5);          // 5指的是LISTENQ监听队列的长度，即表示已完成连接数与正在完成连接数之和不超过5
    assert(ret >= 0);                   // 我们要知道，内核为我们维护两个队列，一个是连接已完成队列，另一个是连接未完成队列
                                        // 这两个队列对应的是TCP的握手过程，三次握手
                                        // 1）客户端往服务器发送SYN报文，服务器处于SYN_RCVD，该客户端的请求即处于连接未完成队列
                                        // 2）服务器往客户端发送SYNACK报文
                                        // 3）客户端往服务器发送ACK报文。此时三次握手完成，连接建立，该请求转移到连接已完成队列
                                        // 而只有当连接已完成时，该listenfd才处于可读状态，epoll通过这个状态来判断listenfd是否可读
                                        // 已完成队列与未完成队列的总数量不超过5，若超过5，还有连接请求，那将会返回错误
    
    
    epoll_event events[MAX_EVENT_NUMBER];   // 用于epoll_wait函数返回已经准备好的事件
    int epollfd = epoll_create(5);      // 5只是告诉内核，epoll表大概需要多大
    assert(epollfd != -1);
    addfd(epollfd, listenfd, false);    // 把listenfd加入了监听表中，当有连接完成了，epoll就返回
    http_conn::m_epollfd = epollfd;

    while (1)
    {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);     // 开始监听，若没有连接发生，将阻塞于此
        if ((number < 0) && (errno != EINTR))       // epoll_wait出错了
        {
            printf("epoll failure!\n");
            break;
        }

        for (int i = 0; i < number; i++)            // 循环处理已准备好的事件
        {
            int sockfd = events[i].data.fd;         // 将sockfd赋值为此次事件的fd
            if (sockfd == listenfd)                 // 如果是listenfd准备好了，即有连接已完成
            {
                struct sockaddr_in client_address;  
                socklen_t client_addrlen = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlen);   // 接收连接请求

                if (connfd < 0)
                {
                    printf("errno is: %d", errno);
                    continue;                       // 如果出错了，则继续处理下一个已准备的事件
                }
                if (http_conn::m_user_count >= MAX_FD)
                {
                    show_error(connfd, "Internal server busy");
                    continue;                       // 如果已连接的用户已经超过了描述符的最大值
                }                                   // 说明此时已经肯定无法建立更多的连接了

                /* 都没有问题的话，就给该连接请求分配一个连接处理实例 */ 
                users[connfd].init(connfd, client_address);
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                users[sockfd].close_conn();         // 如果发现有异常情况，则直接关闭与客户端的连接
                                                    // 用的是close(fd)，优雅关闭方式
            }
            else if (events[i].events & EPOLLIN)    // 如果是读事件就绪了
            {
                if (users[sockfd].read())           // 根据读的结果来判断是否要把任务添加到线程池，还是关闭连接
                {                                   // 因为只有当真正读入了数据时，read才会返回1，出错或者为空等返回0
                    pool->append(users + sockfd);   // 把任务加入到线程池，当线程池为空时，将会阻塞等待任务的到来 
                }                                   // 当把任务加进线程池后，线程池
                else 
                {
                    users[sockfd].close_conn();     // 如果read返回的是0，则直接关闭连接
                }
            }
            else if (events[i].events & EPOLLOUT)   // 如果是写时间就绪了
            {
                if (!users[sockfd].write())         // 那就执行写操作，根据写操作的执行结果来决定是否关闭这个连接
                {
                    users[sockfd].close_conn();     // 如果写操作返回错误，则关闭该连接
                }
                else 
                {   }                               // 如果写入成功，则什么也不用做
            }
        }
    }

    close(epollfd);
    close(listenfd);
    delete [] users;
    delete [] pool;
    
    return 0;
}




























