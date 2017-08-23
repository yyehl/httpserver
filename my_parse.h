#ifndef _MY_PARSE_H_
#define _MY_PARSE_H_

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string>
#include <string.h>
#include <fcntl.h>

/*
*   使用有限状态机思想，解析HTTP头部信息
*/


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



#endif