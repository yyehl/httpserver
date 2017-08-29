#ifndef _MY_PARSE_H_
#define _MY_PARSE_H_

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
#include <iostream>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>

/*
*   使用有限状态机思想，解析HTTP头部信息
*/


class my_parse
{
    friend class my_httpconn;
public: 
    /** 文件名的最大长度 **/
    static const int FILENAME_LEN = 200;
    /** 读缓冲区的大小 **/
    static const int READ_BUFFER_SIZE = 2048;
    /** 写缓冲区的大小 **/
    static const int WRITE_BUFFER_SIZE = 1024;

    /** HTTP请求方法，目前仅支持GET，POST，TRACE **/
    enum METHOD  {  GET = 0,    // 客户请求服务器上的某些资源
                    POST,       // 客户往服务器上提交一些数据
                    HEAD,       // 与GET很像，但是服务器对此请求，只响应头部，而不响应具体资源
                    PUT,        // 客户向服务器写入文档
                    DELETE,     // 客户向服务器请求删除某些资源，但是服务器不一定真的会删除
                    TRACE,      // 服务器把收到的请求信息的副本，精确的装在主体，返回给客户
                    OPTIONS,    // 客户向服务器请求告知其所支持的各种功能
                 };

    /** 解析用户请求时，主状态机所处的状态 **/
    enum CHECK_STATE {  CHECK_STATE_REQUESELINE = 0,   // 
                        CHECK_STATE_HEADER, 
                        CHECK_STATE_CONTENT   
                     };

    /** http服务器处理HTTP请求可能得到的结果 **/
    enum HTTP_CODE   {  NO_REQUEST,         // 表示请求还不完整，需要继续读取客户数据
                        GET_REQUEST,        // 表示获得了一个完整的客户请求
                        BAD_REQUEST,        // 表示客户请求出错，无法完成解析
                        NO_RESOURCE,        // 表示服务器没有客户所请求的资源
                        FORBIDDEN_REQUEST,  // 表示客户请求的资源，被禁止访问
                        INTERNAL_ERROR,     // 表示服务器内部错误
                        CLOSED_CONNECTION   // 表示客户端已关闭连接
                     };

    /** 行读取状态 **/
    enum LINE_STATUS {  LINE_OK,        // 当完整的读入了一行之后的状态
                        LINE_BAD,       // 当读取操作出错是返回的状态
                        LINE_OPEN       // 当读入了一部分，但是还没有读完时的状态
                      };

public:
    my_parse() { init(); }
    ~my_parse() { }


    /** 解析HTTP请求，返回解析处理结果 **/
    HTTP_CODE process_read();
    /** 填充HTTP应答 **/
    bool process_write(HTTP_CODE ret);



private:
    void init();

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

private:
    /** 读缓冲区 **/
    char            m_read_buf[READ_BUFFER_SIZE];
    /** 该缓冲区将要被读入的下一个位置 **/
    int             m_read_idx;
    /** 当前正在分析的字符在缓冲区的位置 **/
    int             m_check_idx;
    /** 当前正在解析的行的起始位置 **/
    int             m_start_line;

    /** 写缓冲区 **/
    char            m_write_buf[WRITE_BUFFER_SIZE];
    /** 写缓冲区中待发送的字节数 **/
    int             m_write_idx;

    /** 状态机当前所处的状态 **/
    CHECK_STATE     m_check_state;
    /** 请求方法 **/
    METHOD          m_method;

    /** 客户群请求的目标文件的完整路径，其内容为doc_root + m_url
        doc_root为网站根目录 **/
    char            m_real_file[FILENAME_LEN];
    /** 客户请求的目标文件文件名 **/
    char*           m_url;
    /** HTTP版本号，仅支持HTTP/1.1 **/
    char*           m_version;
    /** 主机名 **/
    char*           m_host;
    /** HTTP请求消息体的长度 **/
    int             m_content_length;
    /** HTTP请求是否要求保持连接 **/
    bool            m_linger;
    /** 客户请求的目标文件被mmap到内存中的起始位置 **/
    char*           m_file_address;
    /** 目标文件的状态，判断文件是否存在，是否为目录，是否可读，并获取文件大小等信息 **/
    struct stat     m_file_stat;

    /** 将使用writev来执行写操作，所以定义下面两个文件，其中m_iv_count表示被写内存块的数量 **/
    struct iovec    m_iv[2];
    int             m_iv_count;
};

#endif