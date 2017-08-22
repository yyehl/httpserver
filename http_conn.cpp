#include "http_conn.h"

const char* ok_200_title    =      "OK";
const char* error_400_title =      "Bad Request";
const char* error_400_form  =      "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title =      "Forbidden";
const char* error_403_form  =      "You do not have permission to get the file from this server.\n";
const char* error_404_title =      "Not found";
const char* error_404_form  =      "The requested file was not found on this server.\n";
const char* error_500_title =      "Internal Error";
const char* error_500_form  =      "There was an unusual problem serving the requested file.\n";

const char* doc_root = "/var/www/html";

int setnobolcking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void addfd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if (one_shot)
    {
        event.events = event.events | EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
}

void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

void modfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

void http_conn::close_conn(bool real_close)
{
    if (real_close && (m_sockfd != -1))
    {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

void http_conn::init(int sockfd, const sockaddr_in& addr)
{
    m_sockfd = sockfd;
    m_address = addr;

    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    addfd(m_epollfd, sockfd, true);
    m_user_count++;

    init();
}

void http_conn::init()
{
    m_check_state = CHECK_STATE_REQUESELINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_check_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for (; m_check_idx < m_read_idx; ++m_check_idx)
    {
        temp = m_read_buf[m_check_idx];
        if (temp == '\r')                               // \r 表示回到当前行的行首
        {
            if ((m_check_idx + 1) == m_read_idx)        // 
            {
                return LINE_OPEN;
            }
            else if (m_read_buf[m_check_idx + 1] == '\n')  // \n 表示下一行，但不是直接到对首，而是下一行的对应位置
            {
                m_read_buf[m_check_idx++] = '\0';      //
                m_read_buf[m_check_idx++] = '\0';      
                return LINE_OK;
            }
            return LINE_BAD;                           // 
        }
        else if (temp == '\n')                         // 
        {
            if ((m_check_idx > 1) && (m_read_buf[m_check_idx - 1] == '\r'))
            {
                m_read_buf[m_check_idx-1] = '\0';
                m_read_buf[m_check_idx+1] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}


bool http_conn::read()
{
    if (m_read_idx > READ_BUFFER_SIZE)
        return false;
    
    int byte_read = 0;
    while (1)
    {
        byte_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if (byte_read == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            return false;
        }
        else if (byte_read == 0)
        {
            return false;
        }

        m_read_idx += byte_read;
    }
    return true;
}

http_conn::HTTP_CODE http_conn::parse_request_line(char* text)
{
    m_url = strpbrk(text, " \t");       // strpbrk函数为在text中找“\t”字符，找到任意一个就返回，没有找到就返回空指针
    if (!m_url)   
        return BAD_REQUEST;
    *m_url++ = '\0';

    char* method = text;
    if (strcasecmp(method, "GET") == 0)  // strcasecmp函数忽略大小写，比较method的前几个字节与“GET”字符串
    {
        m_method = GET;
    }
    else 
        return BAD_REQUEST;
    
    m_url += strspn(m_url, " \t");    // strspn 函数返回m_url中起始处为\t的字节数，即跳过"\t"

    m_version = strpbrk(m_url, " \t");
    if (!m_version)
        return BAD_REQUEST;
    *m_version++ = '\0';
    *m_version += strspn(m_url, " \t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;
    
    if (strncasecmp(m_url, "http://", 7) == 0)   // 比较m_url与“http://”的前7个字符，忽略大小写
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }

    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;

    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_headers(char* text)
{
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
            m_linger = true;
    }
    else if (strncasecmp(text, "Content-Length:", 15) == 0)
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
        printf("Unkonw header: %s", text);
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_content(char* text)
{
    if (m_read_idx >= (m_content_length + m_check_idx))
    {
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;

    while (((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) || 
                                                ((line_status = parse_line()) == LINE_OK))
    {
        text = get_line();
        m_start_line = m_check_idx;
        printf("got 1 http line: %s\n", text);

        switch (m_check_state)
        {
            case CHECK_STATE_REQUESELINE:
            {
                ret = parse_request_line(text);
                if (ret == BAD_REQUEST)
                    return BAD_REQUEST;
                break;
            }
            case CHECK_STATE_HEADER:
            {
                ret = parse_headers(text);
                if (ret == BAD_REQUEST)
                    return BAD_REQUEST;
                else if (ret == GET_REQUEST)
                    return do_request();
                break;
            }
            case CHECK_STATE_CONTENT:
            {
                ret = parse_content(text);
                if (ret == GET_REQUEST)
                    return do_request;
                line_status = LINE_OPEN;
                break;
            }
            default: 
            {
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request()
{
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    strncpy(m_real_file + len, m_url, FILENAME_LEN-len-1);
    if (stat(m_real_file, &m_file_stat) < 0)
        return NO_REQUEST;
    if (!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;
    if (S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;
    
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

void http_conn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);   // 解除 m_file_address 映射
        m_file_address = 0;
    }
}

bool http_conn::write()
{
    int temp = 0;
    int byte_have_send = 0;
    int byte_to_send = m_write_idx;
    
    if (byte_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while (1)
    {
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp <= -1)
        {
            if (errno == EAGAIN)
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();                    // 如果是其他错误导致的writev返回-1，则解除内存映射，返回false
            return false;
        }
        
        byte_to_send -= temp;
        byte_have_send += temp;
        if (byte_to_send <= byte_have_send)             // 什么意思。。。。。
        {
            unmap();
            if (m_linger)
            {
                init();
                modfd(m_epollfd, m_sockfd, EPOLLIN);
                return true;
            }
            else 
            {
                modfd(m_epollfd, m_sockfd, EPOLLIN);
                return false;
            }
        }
    }
}




