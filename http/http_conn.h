#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H


#include <sys/epoll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/uio.h>
#include <string.h>

#include "../locker/locker.h"

// 网站的根目录
const char* doc_root = "/home/wljszj/webserver/resources";

// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

class http_conn
{
public:
    static int m_epollfd; // 所有的socket上的事件都被注册到同一个epoll事件中
    static int m_user_cnt; // 统计用户数量
    static const int READ_BUFFER_SIZE = 2048; // 读缓冲区的大小
    static const int WRITE_BUFFER_SIZE = 1024; // 写缓冲区的大小
    static const int FILENAME_LEN = 200; // 文件名的最大长度
    

    // HTTP请求方法，这里只支持GET
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};
    
    /* 
        解析客户端请求时，主状态机的状态
        CHECK_STATE_REQUESTLINE:当前正在分析请求行
        CHECK_STATE_HEADER:当前正在分析头部字段
        CHECK_STATE_CONTENT:当前正在解析请求体
    */
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };
    
    /*
        服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST          :   请求不完整，需要继续读取客户数据
        GET_REQUEST         :   表示获得了一个完成的客户请求
        BAD_REQUEST         :   表示客户请求语法错误
        NO_RESOURCE         :   表示服务器没有资源
        FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
        FILE_REQUEST        :   文件请求,获取文件成功
        INTERNAL_ERROR      :   表示服务器内部错误
        CLOSED_CONNECTION   :   表示客户端已经关闭连接了
    */
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };
    
    // 从状态机的三种可能状态，即行的读取状态，分别表示
    // 1.读取到一个完整的行 2.行出错 3.行数据尚且不完整
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

    
    http_conn(){}
    ~http_conn(){}

    void process(); // 处理客户端请求
    void init(int sockfd, const sockaddr_in& addr); // 初始化新接受的连接
    void close_conn(); // 关闭连接
    bool read(); //非阻塞读
    bool write(); //非阻塞写
    void unmap();
    


private:
    int m_sockfd; // 该HTTP连接的socket
    sockaddr_in m_address; // 通信的socket地址

    char m_read_buf[READ_BUFFER_SIZE]; // 读缓冲区
    int m_read_idx; // 标识读缓冲区中已经读入的客户端数据的最后一个字符的下一个字节位置

    char m_write_buf[WRITE_BUFFER_SIZE]; // 写缓冲区
    int m_write_idx; // 写缓冲区中待发送的字节数

    struct iovec m_iv[2]; // 我们将采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量。
    int m_iv_count;

    int m_checked_idx; // 当前正在分析的字符所在读缓冲区的位置
    int m_start_line; // 当前正在解析的行的起始位置

    // 请求得到的内容
    char* m_url; // 请求目标文件的文件名
    char* m_version; // 协议版本，只支持HTTP1.1
    METHOD m_method; // 请求方法 GET POST等
    char* m_host; // 主机名
    bool m_linger; // 判断HTTP请求是否要保持连接
    int m_content_length; // HTTP请求的消息总长度

    CHECK_STATE m_check_state; // 主状态机当前所处状态

    char m_real_file[FILENAME_LEN]; // 客户请求的目标文件的完整路径，其内容等于 doc_root + m_url, doc_root是网站根目录

    struct stat m_file_stat; // 目标文件的状态。通过它我们可以判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息
    char* m_file_address; // 客户请求的目标文件被mmap到内存中的起始位置

    int bytes_to_send; // 将要发送的数据的字节数
    int bytes_have_send; // 已经发送的字节数

    
    void init(); // 初始化其他信息
    HTTP_CODE process_read(); // 解析HTTP请求
    HTTP_CODE parse_request_line(char* text); // 解析请求首行
    HTTP_CODE parse_headers(char* text); // 解析请求头
    HTTP_CODE parse_content(char* text); // 解析请求体
    LINE_STATUS parse_line();
    char* get_line(){return m_read_buf + m_start_line;}
    HTTP_CODE do_request(); // 具体解析

    bool process_write(HTTP_CODE ret);
    // 这一组函数被process_write调用以填充HTTP应答。
    bool add_status_line(int status, const char* title); // 添加响应首行
    bool add_headers(int content_len); // 添加响应头
    bool add_content(const char* content); // 添加响应体
    bool add_response(const char* format, ... ); // 往写缓冲中写入待发送的数据
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();
    bool add_content_type();


};

int http_conn::m_epollfd = -1; // 所有的socket上的事件都被注册到同一个epoll事件中
int http_conn::m_user_cnt = 0; // 统计用户数量

// 设置文件描述符非阻塞
void setnonblocking(int fd)
{
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL ,new_flag);
}

// 向epoll中添加需要监听的文件描述符
void addfd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;
    // event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;


    if(one_shot){
        event.events | EPOLLONESHOT;
    }

    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

// 从epoll中删除监听的文件描述符
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 修改epoll中的文件描述符，重置socket上的EPOLLONESHOT事件，确保下一次可读时，EPOLLIN事件被触发
void modfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

// 初始化新接受的连接
void http_conn::init(int sockfd, const sockaddr_in& addr)
{
    m_sockfd = sockfd;
    m_address = addr;

    // 端口复用
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // 添加到epoll对象中
    addfd(m_epollfd, m_sockfd, true);
    m_user_cnt++;

    init(); // 下面那个init()
}

// 初始化其他一些信息
void http_conn::init()
{
    m_check_state = CHECK_STATE_REQUESTLINE; // 初始化状态为解析请求首行
    m_checked_idx = 0; 
    m_start_line = 0;
    m_read_idx = 0;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_linger = false;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;

    bytes_to_send = 0;
    bytes_have_send = 0;

    bzero(m_read_buf, READ_BUFFER_SIZE); // 读缓冲区清空
    bzero(m_write_buf, READ_BUFFER_SIZE);
    bzero(m_real_file, FILENAME_LEN);
}

// 关闭连接
void http_conn::close_conn()
{
    if(m_sockfd != -1){
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_cnt --;
    }
}

// 非阻塞读
// 循环读取客户数据，直到无数据可读
bool http_conn::read()
{
    if(m_read_idx >= READ_BUFFER_SIZE) return false;

    // 读取到的字节
    int bytes_read = 0;
    while(true){
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0); // 成功返回字节数
        if(bytes_read == -1){
            if(errno == EAGAIN || errno == EWOULDBLOCK) // 没有数据
            {
                break;
            }
            return false;
        }else if(bytes_read == 0){
            // 对方关闭连接
            return false;
        }

        m_read_idx += bytes_read;
    }
    printf("读取到了数据：%s\n", m_read_buf);
    return true;

} 

// 主状态机，解析HTTP请求
http_conn::HTTP_CODE http_conn::process_read()
{
    // 初始状态
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;

    char* text = 0;

    while(((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) ||
     ((line_status = parse_line()) == LINE_OK)) // 解析到了一行完整的数据，或者解析到了请求体，也是完整的数据
    {
        // 获取一行数据
        text = get_line();
        m_start_line = m_checked_idx;
        printf("got 1 http line: %s\n", text);

        // 有限状态机
        switch(m_check_state){
            case CHECK_STATE_REQUESTLINE:{
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST) return BAD_REQUEST;
                break;
            }

            case CHECK_STATE_HEADER:{
                ret = parse_headers(text);
                if(ret == BAD_REQUEST) return BAD_REQUEST;
                else if(ret == GET_REQUEST) return do_request(); // 返回成功，解析具体信息
            }

            case CHECK_STATE_CONTENT:{
                ret = parse_content(text);
                if(ret == GET_REQUEST) return do_request();
                line_status = LINE_OPEN;
                break;
            }

            default: return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;

}

// 解析请求首行,获得 请求方法，目标URL，HTTP版本
http_conn::HTTP_CODE http_conn::parse_request_line(char* text)
{
    // GET /index.html HTTP/1.1
    m_url = strpbrk(text, " \t");
    if(!m_url) return BAD_REQUEST;
    // GET\0/index.html HTTP/1.1
    *m_url++ = '\0';

    char* method = text;
    if(strcasecmp(method, "GET") == 0) m_method = GET;
    else return BAD_REQUEST;

    // /index.html HTTP/1.1
    m_version = strpbrk(m_url, " \t");
    if(!m_version) return BAD_REQUEST;
    // /index.html\0HTTP/1.1
    *m_version++ = '\0';
    if(strcasecmp(m_version, "HTTP/1.1") != 0) return BAD_REQUEST;

    // 如果是 http://192.168.1.1:10000/index.html
    if(strncasecmp(m_url, "http://", 7) == 0){
        m_url += 7; // 192.168.1.1:10000/index.html
        m_url = strchr(m_url, '/'); // /index.html
    }

    // 如果是 https://192.168.1.1:10000/index.html
    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    if(!m_url || m_url[0] != '/') return BAD_REQUEST;

    m_check_state = CHECK_STATE_HEADER; // 请求首行解析完毕，主状态机请求状态变为检查请求头

    return NO_REQUEST; // 还没请求完，下面还有请求头 请求体

}

// 解析请求头
http_conn::HTTP_CODE http_conn::parse_headers(char* text)
{
  // 遇到空行，表示头部字段解析完毕
    if( text[0] == '\0' ) {
        // 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，
        // 状态机转移到CHECK_STATE_CONTENT状态
        if ( m_content_length != 0 ) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则说明我们已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    }else if(strncasecmp(text, "Connection:", 11) == 0){
        // 处理Connection头部字段 Connection: keep-alive
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0) m_linger = true;
    }else if(strncasecmp(text, "Content-Length:", 15) == 0){
        //处理Content-Length头部字段
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }else if(strncasecmp(text, "Host:", 5) == 0){
        // 处理Host头部字段
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }else printf("oop! unknow header %s\n", text);
    
    return NO_REQUEST;
}

// 解析请求体，没有真正解析，只是判断是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char* text)
{
    if (m_read_idx >= (m_content_length + m_checked_idx)){
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 解析一行数据, 判断依据 \r\n
http_conn::LINE_STATUS http_conn::parse_line()
{
    char tmp;
    for(; m_checked_idx < m_read_idx; ++m_checked_idx){
        tmp = m_read_buf[m_checked_idx];
        if(tmp == '\r'){
            if((m_checked_idx+1) == m_read_idx){
                return LINE_OPEN;
            }else if(m_read_buf[m_checked_idx +1]=='\n'){// '\r\n' 回车换行
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            } 
            return LINE_BAD;
        }else if(tmp == '\n'){
            if((m_checked_idx > 1) && (m_read_buf[m_checked_idx - 1] == '\r')){ // '\r\n' 回车换行
                m_read_buf[m_checked_idx-1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        // return LINE_OPEN;

    }
    return LINE_OK;
}

// 当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性，
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其
// 映射到内存地址m_file_address处，并告诉调用者获取文件成功
http_conn::HTTP_CODE http_conn::do_request()
{
    // "/home/wljszj/webserver/resources"
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    // 获取m_real_file文件的相关的状态信息，-1失败，0成功
    if (stat(m_real_file, &m_file_stat) < 0) return NO_RESOURCE;

    // 判断访问权限
    if (!(m_file_stat.st_mode & S_IROTH)) return FORBIDDEN_REQUEST;

    // 判断是否是目录
    if (S_ISDIR(m_file_stat.st_mode)) return BAD_REQUEST;    

    // 以只读方式打开文件
    int fd = open(m_real_file, O_RDONLY);

    // 创建内存映射
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    close(fd);

    return FILE_REQUEST;
}

// 对内存映射区执行munmap操作
void http_conn::unmap()
{
    if(m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

// 处理客户端请求, 由线程池中的工作线程调用，处理HTTP请求的入口函数
void http_conn::process()
{
    // 解析HTTP请求
    HTTP_CODE read_ret = process_read();
    if(read_ret == NO_REQUEST){
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }

    // 生成响应
    bool write_ret = process_write(read_ret);
    if(!write_ret) close_conn();
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}

// 非阻塞写HTTP响应
bool http_conn::write()
{
    int temp = 0;
    int bytes_have_send = 0;    // 已经发送的字节
    int bytes_to_send = m_write_idx;// 将要发送的字节 （m_write_idx）写缓冲区中待发送的字节数
    
    if (bytes_to_send == 0){
        // 将要发送的字节为0，这一次响应结束。
        modfd(m_epollfd, m_sockfd, EPOLLIN); 
        init();
        return true;
    }

    while(1) {
        // 分散写
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp <= -1){
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if(errno == EAGAIN){
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send -= temp;
        bytes_have_send += temp;
        if(bytes_have_send >= m_iv[0].iov_len){
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }else{
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - temp;
        }
        if(bytes_to_send <= 0){
            // 没有数据要发送了
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN);

            if (m_linger){
                init();
                return true;
            }else return false;
        }
    }
}

// 根据服务器处理HTTP请求的结果，决定返回给客户端的内容
bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
        case INTERNAL_ERROR:
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if(!add_content(error_500_form)) return false;
            break;

        case BAD_REQUEST:
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if(!add_content(error_400_form)) return false;
            break;

        case NO_RESOURCE:
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if(!add_content(error_404_form)) return false;
            break;

        case FORBIDDEN_REQUEST:
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if(!add_content(error_403_form)) return false;
            break;

        case FILE_REQUEST:
            add_status_line(200, ok_200_title);
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;

            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;

        default: return false;
    }

    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

// 往写缓冲中写入待发送的数据
bool http_conn::add_response(const char* format, ...)
{
    if(m_write_idx >= WRITE_BUFFER_SIZE) return false;
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if(len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)) return false;
    m_write_idx += len;
    va_end(arg_list);

    return true;
}

// 添加响应首行
bool http_conn::add_status_line(int status, const char* title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_len)
{
    add_content_length(content_len);
    add_content_type();
    add_linger();
    add_blank_line();
}

bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length: %d\r\n", content_len);
}

bool http_conn::add_linger()
{
    return add_response("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close" );
}

bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

bool http_conn::add_content(const char* content)
{
    return add_response("%s", content);
}

bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}


#endif