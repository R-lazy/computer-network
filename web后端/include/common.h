#ifndef TINYSERVER_COMMON_H
#define TINYSERVER_COMMON_H

extern int setNonBlocking(int fd);   //设为非阻塞
extern int addToEpoll(int epoll_fd, int fd, bool oneshot = false);   //监听
extern int removeFromEpoll(int epoll_fd, int fd);  //移除
extern int modFd(int epoll_fd, int fd, int ev);    //修改监听状态
extern void registerSig(int sig, void (*handle)(int), bool restart = true);   //注册信号，进行监听
extern char *int2C_string(int num, char *str);       //整数转化成字符串

class Runner {
public:
    Runner() = default;
    virtual ~Runner() = default;

    virtual void run() = 0;
};

#endif //TINYSERVER_COMMON_H
