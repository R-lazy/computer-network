#include <iostream>
#include <stdexcept>

#include <cstring>
#include <cerrno>
#include <csignal>
#include <dirent.h>
#include <unistd.h>
//#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>

#include "common.h"
#include "http_conn.h"
#include "threadpool.h"

constexpr int max_epoll_events = 1024;
constexpr int max_fd = 65535;

int sig_pipe[2];


//存贮连接并管理
class UserWrapper {
public:
    UserWrapper(int max_fd_num);
    ~UserWrapper();
    HttpConn *operator[](int index);

    int getMaxFd() const;

private:
    HttpConn *m_users;
    int m_max_fd;
};

UserWrapper::UserWrapper(int max_fd_num) : m_max_fd(max_fd_num - 1) {
    m_users = new HttpConn[max_fd_num];
}

UserWrapper::~UserWrapper() {
    delete[]m_users;
}

HttpConn *UserWrapper::operator[](int index) {
    return m_users + index;
}

inline int UserWrapper::getMaxFd() const {
    return m_max_fd;
}

void sigHandler(int sig) {
    int old_err = errno;
    int data = sig;
    send(sig_pipe[1], reinterpret_cast<char *>(&data), 1, 0);
    errno = old_err;
}



int main(int argc, char **argv) {
    chdir("root");
    if (argc != 2)
        throw std::runtime_error("invalid main args");

    HttpConn::prepareResource();    //准备资源
    


    //设置套接字
    int listen_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        printf("%s\n", strerror(errno));
        throw std::runtime_error("cannot create listen fd");
    }

    int status = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &status, sizeof(status));

    struct sockaddr_in listen_address;
    memset(&listen_address, 0, sizeof(listen_address));
    listen_address.sin_family = AF_INET;
    listen_address.sin_addr.s_addr = htonl(INADDR_ANY);
    listen_address.sin_port = htons(atoi(argv[1]));
    std::cout << "port: " << atoi(argv[1]) << std::endl;

    int err = bind(listen_fd, reinterpret_cast<struct sockaddr *>(&listen_address), sizeof(listen_address));
    if (err == -1) {
        printf("%s\n", strerror(errno));
        throw std::runtime_error("bind socket error");
    }
 


//初始化epoll，把要监听的注册到epoll
    err = listen(listen_fd, 5);
    if (err == -1) {
        printf("%s\n", strerror(errno));
        throw std::runtime_error("listen socket error");
    }



//设置监听信号，把要监听信号的注册到epoll
    int epoll_fd = epoll_create(5);
    if (epoll_fd == -1) {
        close(listen_fd);
        printf("%s\n", strerror(errno));
        throw std::runtime_error("create epoll error");
    }
    err = addToEpoll(epoll_fd, listen_fd);

    err = socketpair(PF_UNIX, SOCK_STREAM, 0, sig_pipe);
    if (err == -1) {
        printf("%s\n", strerror(errno));
        throw std::runtime_error("create socketpair error");
    }
    addToEpoll(epoll_fd, sig_pipe[0]);
    setNonBlocking(sig_pipe[1]);
    

    //设置要监听的信号
    registerSig(SIGTERM, sigHandler);
    registerSig(SIGINT, sigHandler);
    registerSig(SIGALRM, sigHandler);

    bool stop = false;

    UserWrapper users(max_fd);   //创建userwr
    struct epoll_event events[max_epoll_events];
    ThreadPool threadPool;     //创建线程池
 

 //循环监听事件
    while (!stop) {
        int n = epoll_wait(epoll_fd, events, max_epoll_events, -1);    
        if (n == -1 && errno != EINTR) {
            break;
        }



//处理链接
        for (int i = 0; i < n; ++i) {
            int sock_fd = events[i].data.fd;   //取出文件描述符
            uint32_t event = events[i].events; //取出事件
            if (sock_fd == listen_fd) {
                // handle new request
                struct sockaddr_in conn_address;
                while (true) {
                    socklen_t conn_size = sizeof(conn_address);
                    int conn_fd = accept(sock_fd, reinterpret_cast<struct sockaddr *>(&conn_address), &conn_size);
                    if (conn_fd == -1) {
                        // accept error
                        break;
                    }
                    std::cout << "accept fd: " << conn_fd << std::endl;
                    if (conn_fd > users.getMaxFd()) {
                        // max conn fd
                        close(conn_fd);
                        break;
                    }
                    users[conn_fd]->init(conn_fd, conn_address, epoll_fd);
                }

            
            
            //处理信号
            } else if (sock_fd == sig_pipe[0] && event & EPOLLIN) {
                // handle signal event
                char signals[1024] = {0};
                auto bytes = recv(sock_fd, signals, sizeof(signals), 0);
                if (bytes == -1 || bytes == 0) {
                    continue;
                } else {
                    for (int j = 0; j < bytes; ++j) {
                        switch (signals[j]) {
                            case SIGALRM:
                                // do something
                                break;
                            case SIGTERM:
                            case SIGINT:
                                stop = true;
                            default:
                                break;
                        }
                    }
                }
            } else if (event & EPOLLIN) {
                // handle EPOLLIN event on conn fd,
                // which is usually http request
                if (users[sock_fd]->readReqToBuf()) {
                    // if success, handle users request and prepare write
                    threadPool.appendTask(users[sock_fd]);
                } else {
                    users[sock_fd]->closeConn();
                }
            } else if (event & EPOLLOUT) {
                // handle EPOLLOUT event on conn fd,
                // which is usually writing http request to client
                if (!users[sock_fd]->writeResp()) {
                    users[sock_fd]->closeConn();
                }
            } else if (event & (EPOLLERR | EPOLLRDHUP)) {
                // handle error event
                users[sock_fd]->closeConn();
                removeFromEpoll(epoll_fd, sock_fd);
            } else {
                // handle unsupported event
                users[sock_fd]->closeConn();
                removeFromEpoll(epoll_fd, sock_fd);
            }
        }
    }

    close(epoll_fd);
    return 0;
}
