#include "my_httpconn.h"

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

int my_httpconn::m_user_count = 0;
int my_httpconn::m_epollfd = -1;

void my_httpconn::close_conn(bool real_close)
{
    if (real_close && (m_sockfd != -1))
    {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

void my_httpconn::init(int sockfd, const sockaddr_in& addr)
{
    m_sockfd = sockfd;
    m_address = addr;

    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    addfd(m_epollfd, sockfd, true);
    m_user_count++;

    m_parse = new my_parse();
}

bool my_httpconn::read()
{
    if (m_parse->m_read_idx > m_parse->READ_BUFFER_SIZE)
        return false;
    
    int byte_read = 0;
    while (1)
    {
        byte_read = recv(m_sockfd, m_parse->m_read_buf + m_parse->m_read_idx, 
                        m_parse->READ_BUFFER_SIZE - m_parse->m_read_idx, 0);
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

        m_parse->m_read_idx += byte_read;
    }
    return true;
}


bool my_httpconn::write()
{
    int temp = 0;
    int byte_have_send = 0;
    int byte_to_send = m_parse->m_write_idx;
    
    if (byte_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        m_parse->init();
        return true;
    }

    while (1)
    {
        temp = writev(m_sockfd, m_parse->m_iv, m_parse->m_iv_count);
        if (temp <= -1)
        {
            if (errno == EAGAIN)
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            m_parse->unmap();                    // 如果是其他错误导致的writev返回-1，则解除内存映射，返回false
            return false;
        }
        
        byte_to_send -= temp;
        byte_have_send += temp;
        if (byte_to_send <= byte_have_send)             
        {
            m_parse->unmap();
            if (m_parse->m_linger)
            {
                m_parse->init();
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

/** 由线程池的工作线程调用，这是处理HTTP请求的入口函数 **/
void my_httpconn::process()
{
    my_parse::HTTP_CODE read_ret = m_parse->process_read();
    if (read_ret == my_parse::NO_REQUEST)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return ;
    }

    bool write_ret = m_parse->process_write(read_ret);
    if (!write_ret)
    {
        close_conn();
    }

    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}



