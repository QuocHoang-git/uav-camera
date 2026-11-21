#define _GNU_SOURCE

#include "poller.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/timerfd.h>

eipc_status_t eipc_poller_init(eipc_poller_t *poller) {
    if (poller == NULL) {
        return EIPC_ERR_INVAL;
    }
    poller->epoll_fd = -1;
    poller->timer_fd = -1;
    
    poller->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (poller->epoll_fd < 0) {
        perror("[EIPC Poller] epoll_create1() failed");
        return EIPC_ERR_IO;
    }
    
    poller->timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (poller->timer_fd < 0) {
        perror("[EIPC Poller] timerfd_create() failed");
        close(poller->epoll_fd);
        poller->epoll_fd = -1;
        return EIPC_ERR_IO;
    }
    
    if (eipc_poller_add(poller, poller->timer_fd, EPOLLIN) != EIPC_OK) {
        eipc_poller_deinit(poller);
        return EIPC_ERR_IO;
    }
    return EIPC_OK;
}

void eipc_poller_deinit(eipc_poller_t *poller) {
    if (poller == NULL) {
        return;
    }
    if (poller->timer_fd >= 0) {
        if (poller->epoll_fd >= 0) {
            epoll_ctl(poller->epoll_fd, EPOLL_CTL_DEL, poller->timer_fd, NULL);
        }
        close(poller->timer_fd);
        poller->timer_fd = -1;
    }
    if (poller->epoll_fd >= 0) {
        close(poller->epoll_fd);
        poller->epoll_fd = -1;
    }
}

eipc_status_t eipc_poller_add(eipc_poller_t *poller, int fd, uint32_t events) {
    if (poller == NULL || poller->epoll_fd < 0 || fd < 0) {
        return EIPC_ERR_INVAL;
    }

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(poller->epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        perror("[EIPC Poller] epoll_ctl ADD failed");
        return EIPC_ERR_IO;
    }

    return EIPC_OK;
}

eipc_status_t eipc_poller_del(eipc_poller_t *poller, int fd) {
    if (poller == NULL || poller->epoll_fd < 0 || fd < 0) {
        return EIPC_ERR_INVAL;
    }

    if (epoll_ctl(poller->epoll_fd, EPOLL_CTL_DEL, fd, NULL) < 0) {
        if (errno != EBADF && errno != ENOENT) {
            perror("[EIPC Poller] epoll_ctl DEL failed");
            return EIPC_ERR_IO;
        }
    }
    return EIPC_OK;
}

void eipc_poller_arm_timer(eipc_poller_t *poller, uint32_t interval_sec) {
    if (poller == NULL || poller->timer_fd < 0 || interval_sec == 0) {
        return;
    }
    struct itimerspec new_value;
    memset(&new_value, 0, sizeof(new_value));
    
    new_value.it_value.tv_sec = interval_sec;
    new_value.it_value.tv_nsec = 0;
    
    new_value.it_interval.tv_sec = interval_sec;
    new_value.it_interval.tv_nsec = 0;
    timerfd_settime(poller->timer_fd, 0, &new_value, NULL);
}

void eipc_poller_disarm_timer(eipc_poller_t *poller) {
    if (poller == NULL || poller->timer_fd < 0) {
        return;
    }

    struct itimerspec zero_value;
    memset(&zero_value, 0, sizeof(zero_value));
    timerfd_settime(poller->timer_fd, 0, &zero_value, NULL);
}

uint64_t eipc_poller_consume_timer(eipc_poller_t *poller) {
    if (poller == NULL || poller->timer_fd < 0) {
        return 0;
    }
    uint64_t expirations = 0;
    ssize_t s = read(poller->timer_fd, &expirations, sizeof(expirations));
    if (s < 0) {
        return 0;
    }
    return expirations;
}

int eipc_poller_wait(eipc_poller_t *poller, struct epoll_event *events, int max_events, int timeout_ms) {
    if (poller == NULL || poller->epoll_fd < 0 || events == NULL || max_events <= 0) {
        return -1;
    }
    int n_events = epoll_wait(poller->epoll_fd, events, max_events, timeout_ms);
    if (n_events < 0) {
        if (errno == EINTR) {
            return 0;
        }
        perror("[EIPC Poller] epoll_wait() failed");
        return -1;
    }
    return n_events;
}
