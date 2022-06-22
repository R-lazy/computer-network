#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <assert.h>
#include <sys/epoll.h>

#include "common.h"

int setNonBlocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

int addToEpoll(int epoll_fd, int fd, bool oneshot) {
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLERR | EPOLLRDHUP | EPOLLET;
    if (oneshot)
        event.events |= EPOLLONESHOT;
    int ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
    setNonBlocking(fd);
    return ret;
}

int removeFromEpoll(int epoll_fd, int fd) {
    return epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
}

int modFd(int epoll_fd, int fd, int ev) {
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLERR | EPOLLRDHUP;
    return epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event);
}

void registerSig(int sig, void (*handle)(int), bool restart) {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handle;
    if (restart)
        action.sa_flags = SA_RESTART;
    sigfillset(&action.sa_mask);
    assert(sigaction(sig, &action, nullptr) != -1);
}

char *int2C_string(int num, char *str) {
    int i = 0;
    if (num < 0) {
        num = -num;
        str[i++] = '-';
    }
    do {
        str[i++] = num % 10 + 48;
        num /= 10;
    } while (num);

    str[i] = '\0';

    int j = 0;
    if (str[0] == '-') {
        j = 1;
        ++i;
    }
    for (; j < i / 2; j++) {
        str[j] = str[j] + str[i - 1 - j];
        str[i - 1 - j] = str[j] - str[i - 1 - j];
        str[j] = str[j] - str[i - 1 - j];
    }

    return str;
}
