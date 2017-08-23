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

class my_httpconn
{
public:
    /** 文件名的最大长度 **/
    static const int FILENAME_LEN = 200;
    /** 读缓冲区的大小 **/
    static const int READ_BUFFER_SIZE = 2048;
    /** 写缓冲区的大小 **/
    static const int WRITE_BUFFER_SIZE = 1024;
    
    /** HTTP请求方法，目前仅支持GET **/
    enum METHOD      {  GET = 0, 
                        POST, 
                        HEAD, 
                        PUT,    
                        DELETE, 
                        TRACE, 
                        OPTIONS, 
                        CONNECT, 
                        PATCH        
                     };

    /** 解析用户请求时，主状态机所处的状态 **/
    enum CHECK_STATE {  CHECK_STATE_REQUESELINE = 0, 
                        CHECK_STATE_HEADER, 
                        CHECK_STATE_CONTENT   
                     };

    /** http服务器处理HTTP请求可能得到的结果 **/
    enum HTTP_CODE   {  NO_REQUEST,
                        GET_REQUEST,
                        BAD_REQUEST,
                        NO_RESOURCE,
                        FORBIDDEN_REQUEST,
                        FILE_REQUEST,
                        INTERNAL_ERROR,
                        CLOSED_CONNECTION    
                     };

    /** 行读取状态 **/
    enum LINE_STATUS {  LINE_OK,
                        LINE_BAD,
                        LINE_OPEN    
                      };

public:
    my_httpconn() { }         // 因为没有动态分配内存，都是POD类型，构造函数可以为默认
    ~my_httpconn() { }        // 析构函数

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

private:     // 内部调用函数

    /** 初始化连接 **/
    void init();
    /** 解析HTTP请求，返回解析处理结果 **/
    HTTP_CODE process_read();
    /** 填充HTTP应答 **/
    bool process_write(HTTP_CODE ret);

    /** 用以分析HTTP请求的函数，被 process_read 调用 **/
    HTTP_CODE parse_request_line(char* text);
    HTTP_CODE parse_headers(char* text);
    HTTP_CODE parse_content(char* text);
    HTTP_CODE do_request();
    char* get_line();
    LINE_STATUS parse_line();

    /** 用以填充HTTP应答的内部调用函数，被 process_write 调用 **/
    void unmap();
    bool add_response(const char* format, ...);
    bool add_content(const char* content);
    bool add_status_line(int status, const char* title);
    bool add_headers(int content_len);
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

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

    /** 读缓冲区 **/
    char                        m_read_buf[READ_BUFFER_SIZE];
    /** 该缓冲区将要被读入的下一个位置 **/
    int                         m_read_idx;
    /** 当前正在分析的字符在缓冲区的位置 **/
    int                         m_check_idx;
    /** 当前正在解析的行的起始位置 **/
    int                         m_start_line;

    /** 写缓冲区 **/
    char                        m_write_buf[WRITE_BUFFER_SIZE];
    /** 写缓冲区中待发送的字节数 **/
    int                         m_write_idx;

    /** 状态机当前所处的状态 **/
    CHECK_STATE                 m_check_state;
    /** 请求方法 **/
    METHOD                      m_method;

    /** 客户群请求的目标文件的完整路径，其内容为doc_root + m_url，doc_root为网站根目录 **/
    char                        m_real_file[FILENAME_LEN];
    /** 客户请求的目标文件文件名 **/
    char*                       m_url;
    /** HTTP版本号，仅支持HTTP/1.1 **/
    char*                       m_version;
    /** 主机名 **/
    char*                       m_host;
    /** HTTP请求消息体的长度 **/
    int                         m_content_length;
    /** HTTP请求是否要求保持连接 **/
    bool                        m_linger;
    /** 客户请求的目标文件被mmap到内存中的起始位置 **/
    char*                       m_file_address;
    /** 目标文件的状态，判断文件是否存在，是否为目录，是否可读，并获取文件大小等信息 **/
    struct stat                 m_file_stat;

    /** 将使用writev来执行写操作，所以定义下面两个文件，其中m_iv_count表示被写内存块的数量 **/
    struct iovec                m_iv[2];
    int                         m_iv_count;
};


#endif 