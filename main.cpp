#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/epoll.h>
#include <signal.h>
#include <assert.h>

#include "http/http_conn.h"
#include "locker/locker.h"
#include "threadpool/threadpool.h"

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000

// 添加信号捕捉
void addsig(int sig, void(handler)(int))
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    assert( sigaction( sig, &sa, NULL ) != -1 );
}

// 添加文件描述符到epoll中
extern void addfd(int epollfd, int fd, bool one_shot);

// 从epoll中删除文件描述符
extern void removefd(int epollfd, int fd);

// 修改文件描述符
extern void modfd(int epollfd, int fd, int ev);

int main(int argc, char* argv[])
{
    if( argc <=1)
    {
        printf("按照如下格式运行：%s port_number\n", basename(argv[0]));
        exit(-1);
    }
    printf("port");
    int port = atoi(argv[1]);
    

    // 对SIGPIPE信号进行处理
    addsig(SIGPIPE, SIG_IGN);

    // 创建线程池并初始化
    threadpool<http_conn>* pool = NULL;


    try{
        pool = new threadpool<http_conn>();
    }catch(...)
    {

        exit(-1);
    }

    // 创建一个数组用于保存所有的客户端信息
    http_conn* users = new http_conn[MAX_FD];

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);


    // 设置端口复用
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    bind(listenfd, (struct sockaddr*)&address, sizeof(address));


    listen(listenfd, 5);

    // 创建epoll对象， 事件数组， 添加
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);

    // 将监听的文件描述符添加到epoll中
    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;

    while(true)
    {
        int num = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if((num<0) && (errno != EINTR)){
            printf("epoll failure!\n");
            break;
        }

        // 循环遍历事件数组
        for(int i=0; i<num; ++i){
            int sockfd = events[i].data.fd;
            if(sockfd == listenfd){
                // 有客户端连接进来
                struct sockaddr_in client_address;
                socklen_t client_addr_len = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addr_len);

                if ( connfd < 0 ) {
                    printf( "errno is: %d\n", errno );
                    continue;
                } 

                // 连接成功
                if(http_conn::m_user_cnt >= MAX_FD){
                    // 目前连接数满了
                    // 可以告诉客户端，服务器正忙
                    close(connfd);
                    continue;
                }

                // 将新的客户的数据初始化，放到数组中，将文件描述符当成索引
                users[connfd].init(connfd, client_address);
            }
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            // 对方异常断开或者错误等事件
            {
                users[sockfd].close_conn();
            }
            else if(events[i].events & EPOLLIN)
            {
                if(users[sockfd].read()){
                    // 一次性把所有数据读完
                    pool->append(users + sockfd);
                }else{
                    users[sockfd].close_conn();
                }
            }
            else if(events[i].events & EPOLLOUT){
                if(!users[sockfd].write()){ // 一次性写完所有数据
                    users[sockfd].close_conn();
                }
            }
        }

    }

    close(epollfd);
    close(listenfd);
    delete [] users;
    delete pool;

    return 0;
}