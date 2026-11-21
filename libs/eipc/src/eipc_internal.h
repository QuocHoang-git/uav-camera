#ifndef __EIPC_INTERNAL_H
#define __EIPC_INTERNAL_H

#include "eipc/eipc.h"
#include "peer_table.h"
#include "poller.h"
#include "frame.h"

#define EIPC_MAX_SOCKET_PATH_SIZE (108)

struct eipc_context {
    int self_fd;
    eipc_poller_t poller;
    
    eipc_state_t state;
    char socket_path[EIPC_MAX_SOCKET_PATH_SIZE];
    
    eipc_role_t role;
    eipc_callbacks_t callbacks;
    eipc_peer_table_t peer_table;
    
    uint8_t rx_buffer[EIPC_FRAME_MAX_SIZE];
    int     rx_fds[EIPC_MAX_FD_TRANSFER];
};

#endif
