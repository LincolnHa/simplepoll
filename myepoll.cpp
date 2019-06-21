#include <iostream>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include "stdlib.h"
#include "string.h"

#define BACKLOGNUM 30    //socket 侦听队列的长度(backlog指定的是已经连接但等待accept的队列的最大值, 如果半连接队列已经满了，此时一个连接请求到达服务端，此时客户端会收到一个ECONNREFUSED的错误码，或者如果此协议支持重传，那么服务端直接忽略掉这个连接请求以使得重传到直到连接成功)
#define MAXEVENTNUM 2000 //events数量
#define PORT 9090 		 //端口

using namespace std;

int main()
{
    int retcode = -1;

    //1. 创建epoll句柄(文件描述符)
    int epfd = epoll_create(255);
    if (epfd < 0)
    {
        printf("创建epoll失败");
        return 0;
    }

    //2. 注册epoll事件
    struct epoll_event ev;
    int listenfd = -1;

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0)
    {
        printf("创建socket失败");
        return 0;
    }

    int on = 1;
    retcode = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)); //SO_REUSEADDR 使得 避开 TIME_WAIT 状态。能够给套接字应用 SO_REUSEADDR 套接字选项，以便port能够立即重用,否则就要等几秒后才可重新bind该端口

    ev.data.fd = listenfd;
    ev.events = EPOLLIN | EPOLLET; //events字段表示感兴趣的事件和被触发的事件可能的取值, 其中
                                   //EPOLLIN: 表示对应的文件描述符可以读,
                                   //EPOLLET: 表示对应的文件描述符设定为edge模式;
                                   //EPOLLONESHOT：只监听一次事件，当监听完这次事件之后，如果还需要继续监听这个socket的话，需要再次把这个socket加入到EPOLL队列里

    epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev); //EPOLL_CTL_ADD: 要进行的操作 是加事件, 该事件是ev

    //3. 对listenfd 进行bind 和 listen
    struct sockaddr_in address;
    bzero(&address, sizeof(address));

    address.sin_family = AF_INET;
    address.sin_port = htons(PORT);
    address.sin_addr.s_addr = htons(INADDR_ANY);

    retcode = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
    if (retcode < 0)
    {
        printf("bind错误");
        return 0;
    }

    retcode = listen(listenfd, BACKLOGNUM);
    if (retcode < 0)
    {
        printf("listen错误");
        return 0;
    }

    int i = 0;
    int newconnfd = 0;
    int eventnum = 0;
    epoll_event events[MAXEVENTNUM];

    //开始获取事件
    while (true)
    {
        eventnum = epoll_wait(epfd, events, MAXEVENTNUM, 100);

        for (i = 0; i < eventnum; i++)
        {
            printf("开始事件处理\n");

            int socketfd = events[i].data.fd;

            //有新的连接, 为它新注册 ev
            if (socketfd == listenfd)
            {
                cout << "新的client 连接"
                     << "\n"
                     << endl;

                struct sockaddr_in clientaddr;
                socklen_t clientaddr_len = sizeof(clientaddr);

                newconnfd = accept(listenfd, (sockaddr *)&clientaddr, &clientaddr_len);
                if (newconnfd < 0)
                {
                    printf("newconnfd error\n");
                    continue;
                }

                //打印新连接的客户端地址
                char *str = inet_ntoa(clientaddr.sin_addr);
                cout << "新的连接, ip:" << str << "\n"
                     << endl;

                epoll_event ev;
                ev.data.fd = newconnfd;
                ev.events = EPOLLIN | EPOLLET;

                //注册新 ev
                epoll_ctl(epfd, EPOLL_CTL_ADD, newconnfd, &ev);
            }
            //之前的事件, 有数据到来, 读取 然后修改 ev 类型
            else if (events[i].events & EPOLLIN)
            {
                cout << "EPOLLIN 事件"
                     << "\n"
                     << endl;

                if (socketfd < 0)
                    continue;

                //读取数据, 暂时先读一次(后续得解析数据, 不断的累计读)
                char line[2048];
                int n = recv(socketfd, line, sizeof(line), 0);
                if (n < 0)
                {
                    printf("recv错误\n");
                    close(socketfd);
                    events[i].data.fd = -1;
                    continue;
                }

                //对方已断开
                if (errno == ECONNRESET)
                {
                    printf("已断开\n");
                    close(socketfd);
                    events[i].data.fd = -1;
                    continue;
                }

                printf("读到数据: %s\n", line);

                epoll_event ev;
                ev.data.fd = socketfd;
                ev.events = EPOLLOUT | EPOLLET;

                //重新修改ev 为 发送类型
                epoll_ctl(epfd, EPOLL_CTL_MOD, socketfd, &ev);
            }
            //有事件要 发送数据
            else if (events[i].events & EPOLLOUT)
            {
                cout << "EPOLLOUT 事件"
                     << "\n"
                     << endl;

                if (socketfd < 0)
                    continue;

                string szTemp = "Server:回写\n";
                int n = send(socketfd, szTemp.c_str(), szTemp.size(), MSG_NOSIGNAL);
                if (n < 0)
                {
                    printf("send错误\n");
                    close(socketfd);
                    events[i].data.fd = -1;
                    continue;
                }

                //非http 重新监听
                // epoll_event ev;
                // ev.data.fd = socketfd;
                // ev.events = EPOLLIN | EPOLLET;

                // //重新修改ev为 读
                // epoll_ctl(epfd, EPOLL_CTL_MOD, socketfd, &ev);

                //http(请求-响应) 关闭此连接 
                epoll_ctl(epfd, EPOLL_CTL_DEL, socketfd, 0);
                close(socketfd);
                events[i].data.fd = -1;
                continue;
            }
            else
            {
                printf("其他事件\n");
            }
        }
    }

    close(epfd);
    close(listenfd);
    return 0;
}