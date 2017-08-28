
#include "my_parse.h"


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

void my_parse::init()
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

my_parse::LINE_STATUS my_parse::parse_line()
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

my_parse::HTTP_CODE my_parse::parse_request_line(char* text)
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

my_parse::HTTP_CODE my_parse::parse_headers(char* text)
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

my_parse::HTTP_CODE my_parse::parse_content(char* text)
{
    if (m_read_idx >= (m_content_length + m_check_idx))
    {
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

my_parse::HTTP_CODE my_parse::process_read()
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
                    return do_request();
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

my_parse::HTTP_CODE my_parse::do_request()
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
    return GET_REQUEST;
}

void my_parse::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);   // 解除 m_file_address 映射
        m_file_address = 0;
    }
}

bool my_parse::add_response(const char* format, ...)
{
    if (m_write_idx >= WRITE_BUFFER_SIZE)   // 写缓冲区里待发送的字节已经大于等于写缓冲区的大小了，即写缓冲区满了
        return false;
    
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE-1-m_write_idx, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE-1-m_write_idx))
        return false;
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

bool my_parse::add_status_line(int status, const char* title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool my_parse::add_headers(int content_len)
{
    add_content_length(content_len);
    add_linger();
    add_blank_line();
}

bool my_parse::add_content_length(int content_len)
{
    return add_response("Content-Length: %d\r\n", content_len);
}

bool my_parse::add_linger()
{
    return add_response("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

bool my_parse::add_blank_line()
{
    return add_response("%s", "\r\n");
}

bool my_parse::add_content(const char* content)
{
    return add_response("%s", content);
}

bool my_parse::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
        case INTERNAL_ERROR: 
        {
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if (!add_content(error_500_form))
            {
                return false;
            }
            break;
        }
        case BAD_REQUEST: 
        {
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if (!add_content(error_400_form))
            {
                return false;
            }
            break;
        }
        case NO_RESOURCE: 
        {
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form));
            {
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST: 
        {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form))
            {
                return false;
            }
            break;
        }
        case GET_REQUEST:
        {
            add_status_line(200, ok_200_title);
            if (m_file_stat.st_size != 0)
            {
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                return true;
            }
            else 
            {
                const char* ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if (!add_content(ok_string));
                {
                    return false;
                }
            }
        }
        default: 
        {
            return false;
        }
    }

    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}