#ifndef __EIPC_POLLER_H
#define __EIPC_POLLER_H
#include <stdint.h>
#include <sys/epoll.h>
#include "eipc/types.h"

typedef struct {
    int epoll_fd;
    int timer_fd;
} eipc_poller_t;

eipc_status_t eipc_poller_init(eipc_poller_t *poller);
void          eipc_poller_deinit(eipc_poller_t *poller);

eipc_status_t eipc_poller_add(eipc_poller_t *poller, int fd, uint32_t events);
eipc_status_t eipc_poller_del(eipc_poller_t *poller, int fd);

void eipc_poller_arm_timer(eipc_poller_t *poller, uint32_t interval_sec);
void eipc_poller_disarm_timer(eipc_poller_t *poller);

uint64_t eipc_poller_consume_timer(eipc_poller_t *poller);

int  eipc_poller_wait(eipc_poller_t *poller, struct epoll_event *events, int max_events, int timeout_ms);

#endif
