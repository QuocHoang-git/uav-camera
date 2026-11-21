#define _GNU_SOURCE

#include "eipc/eipc.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "transport_unix.h"
#include "eipc_internal.h"

static const char* eipc_state_to_str(eipc_state_t state) {
    switch (state) {
        case EIPC_STATE_UNINITIALIZED: return "UNINITIALIZED";
        case EIPC_STATE_DISCONNECTED:  return "DISCONNECTED";
        case EIPC_STATE_CONNECTED:     return "CONNECTED";
        case EIPC_STATE_ERROR:         return "ERROR";
        default:                       return "UNKNOWN";
    }
}

static eipc_status_t eipc_add_peer(eipc_context_t* ctx, int fd){
    eipc_peer_t* peer = eipc_peer_table_add(&ctx->peer_table, fd);
    if (peer == NULL) {
        return EIPC_ERR_FULL;
    }
    
    if (ctx->self_fd != fd) {
        eipc_status_t ret = eipc_poller_add(&ctx->poller, fd, EPOLLIN | EPOLLRDHUP);
        if (ret != EIPC_OK) {
            eipc_peer_table_remove(&ctx->peer_table, fd);
            return ret;
        }
    }
    return EIPC_OK;
}

static eipc_status_t eipc_remove_peer(eipc_context_t* ctx, eipc_peer_t* peer){
    if(peer->fd < 0){
        return EIPC_ERR_INVAL;
    }
    eipc_poller_del(&ctx->poller, peer->fd);
    eipc_peer_table_remove(&ctx->peer_table, peer->fd);
    return EIPC_OK;
}


static bool eipc_is_valid_transition(eipc_state_t old_state, eipc_state_t new_state) {
    if (old_state == new_state) {
        return true; // Giữ nguyên trạng thái
    }
    switch (old_state) {
        case EIPC_STATE_UNINITIALIZED:
            return (new_state == EIPC_STATE_DISCONNECTED || new_state == EIPC_STATE_ERROR);
        case EIPC_STATE_DISCONNECTED:
            return (new_state == EIPC_STATE_CONNECTED || 
                    new_state == EIPC_STATE_UNINITIALIZED || 
                    new_state == EIPC_STATE_ERROR);
        case EIPC_STATE_CONNECTED:
            return (new_state == EIPC_STATE_DISCONNECTED || 
                    new_state == EIPC_STATE_UNINITIALIZED || 
                    new_state == EIPC_STATE_ERROR);
        case EIPC_STATE_ERROR:
            return (new_state == EIPC_STATE_UNINITIALIZED);
        default:
            return false;
    }
}

static eipc_status_t eipc_transite_state(eipc_context_t* ctx, eipc_state_t new_state){
    if (ctx == NULL) {
        return EIPC_ERR_INVAL;
    }
    eipc_state_t old_state = ctx->state;

    if (old_state == new_state) {
        return EIPC_OK;
    }

    if (!eipc_is_valid_transition(old_state, new_state)) {
        fprintf(stderr, "[EIPC FSM] Invalid state transition: %s -> %s\n",
                eipc_state_to_str(old_state), eipc_state_to_str(new_state));
        return EIPC_ERR_NOT_PERMITTED;
    }

    switch (new_state) {
        case EIPC_STATE_CONNECTED:
            eipc_poller_disarm_timer(&ctx->poller);
            break;
        case EIPC_STATE_DISCONNECTED:
            if (ctx->role == EIPC_ROLE_CLIENT) {
                if(ctx->self_fd >= 0){
                    close(ctx->self_fd);
                    ctx->self_fd = -1;
                }
            }
            eipc_poller_arm_timer(&ctx->poller, 2);
            break;
        case EIPC_STATE_UNINITIALIZED:
        case EIPC_STATE_ERROR:
            eipc_poller_disarm_timer(&ctx->poller);
            break;
        default:
            break;
    }
    
    ctx->state = new_state;
    printf("[EIPC FSM] State: %s -> %s\n", 
           eipc_state_to_str(old_state), eipc_state_to_str(new_state));
    return EIPC_OK;
}

static eipc_status_t eipc_on_new_connection(eipc_context_t* ctx, int fd){
    eipc_status_t ret = eipc_add_peer(ctx, fd);
    if(ret != EIPC_OK){
        close(fd);
        return ret;
    }

    if (ctx->callbacks.on_connect) {
        ctx->callbacks.on_connect(ctx, fd, ctx->callbacks.userdata);
    }

    return EIPC_OK;
}

static eipc_status_t eipc_on_disconnect(eipc_context_t* ctx, int peer_fd){
    eipc_peer_t* peer = eipc_peer_table_get_by_fd(&ctx->peer_table, peer_fd);
    if(peer == NULL){
        return EIPC_ERR_DISCONNECTED;
    }
    if(peer_fd == ctx->self_fd){
        eipc_transite_state(ctx, EIPC_STATE_DISCONNECTED);
    }
    if (ctx->callbacks.on_disconnect) {
        ctx->callbacks.on_disconnect(ctx, peer_fd, ctx->callbacks.userdata);
    }
    eipc_remove_peer(ctx, peer);

    return EIPC_OK;
}

static eipc_status_t eipc_on_msg(eipc_context_t* ctx, int peer_fd){
    eipc_status_t ret = EIPC_OK;
    eipc_peer_t* peer = eipc_peer_table_get_by_fd(&ctx->peer_table, peer_fd);
    if(peer == NULL){
        return EIPC_ERR_DISCONNECTED;
    }

    eipc_message_t msg;
    eipc_message_init(&msg);
    ret = eipc_peer_receive(peer, 
                            ctx->rx_buffer, sizeof(ctx->rx_buffer),
                            ctx->rx_fds, EIPC_MAX_FD_TRANSFER,
                            &msg);
    if(ret == EIPC_ERR_DISCONNECTED){
        eipc_on_disconnect(ctx, peer_fd);;
    }

    if(ret != EIPC_OK){
        return ret;
    }

    if(msg.type == EIPC_FRAME_TYPE_SERVICE_FD){
        if(msg.num_fds <= 0){
            return EIPC_ERR_IO;
        }

        for(int i = 0; i < msg.num_fds; ++i){
            eipc_on_new_connection(ctx, msg.fds[i]);
        }
    }
    else{
        if (ctx->callbacks.on_msg) {
            ctx->callbacks.on_msg(ctx, peer_fd, &msg, ctx->callbacks.userdata);
        }
    }

    return ret;
}

static eipc_status_t eipc_broker_connect(eipc_context_t* broker){
    broker->self_fd = eipc_unix_socket_listen(broker->socket_path, EIPC_MAX_CLIENTS);
    if(broker->self_fd < 0){
        return EIPC_ERR_IO;
    }
    
    eipc_poller_add(&broker->poller, broker->self_fd, EPOLLIN);

    return EIPC_OK;
}

static eipc_status_t eipc_client_connect(eipc_context_t* client){
    client->self_fd = eipc_unix_socket_connect(client->socket_path);
    if(client->self_fd < 0){
        return EIPC_ERR_IO;
    }

    eipc_poller_add(&client->poller, client->self_fd, EPOLLIN | EPOLLRDHUP);
    eipc_on_new_connection(client, client->self_fd);

    return EIPC_OK;
}

eipc_status_t eipc_connect(eipc_context_t* ctx){
    if(ctx == NULL || ctx->state != EIPC_STATE_DISCONNECTED){
        return EIPC_ERR_INVAL;
    }
    eipc_status_t ret = EIPC_OK;
    if(ctx->role == EIPC_ROLE_BROKER){
        ret = eipc_broker_connect(ctx);
    }
    else{
        ret = eipc_client_connect(ctx);
    }
    
    if(ret != EIPC_OK){
        return ret;
    }
    
    eipc_transite_state(ctx, EIPC_STATE_CONNECTED);
    return ret;
}

eipc_context_t* eipc_create(eipc_config_t *config){
    if(!config || strlen(config->socket_path) >= EIPC_MAX_SOCKET_PATH_SIZE){
        return NULL;
    }

    if(config->role != EIPC_ROLE_BROKER && config->role != EIPC_ROLE_CLIENT){
        return NULL;
    }

    eipc_context_t *ctx = (eipc_context_t *)calloc(1, sizeof(eipc_context_t));
    if (!ctx) {
        perror("[EPIC Broker] Failed to allocate memory");
        return NULL;
    }

    ctx->self_fd = -1;
    eipc_peer_table_init(&ctx->peer_table);

    strncpy(ctx->socket_path, config->socket_path, sizeof(ctx->socket_path) - 1);
    ctx->callbacks = config->callbacks;
    ctx->role = config->role;

    ctx->state = EIPC_STATE_UNINITIALIZED;
    return ctx;
}

eipc_status_t eipc_destroy(eipc_context_t* ctx){
    if(ctx == NULL){
        return EIPC_ERR_INVAL;
    }

    eipc_deinit(ctx);
    free(ctx);
    return EIPC_OK;
}

eipc_state_t eipc_get_state(eipc_context_t* ctx){
    if(ctx == NULL){
        return EIPC_STATE_INVALID;
    }
    return ctx->state;
}

int eipc_get_epollfd(eipc_context_t* ctx){
    if(ctx == NULL){
        return -1;
    }
    return ctx->poller.epoll_fd;
}

eipc_status_t eipc_init(eipc_context_t* ctx){
    if(ctx == NULL){
        return EIPC_ERR_INVAL;
    }

    eipc_status_t ret = eipc_poller_init(&ctx->poller);
    if(ret != EIPC_OK){
        return ret;
    }

    eipc_transite_state(ctx, EIPC_STATE_DISCONNECTED);
    eipc_connect(ctx);

    return ret;
}

eipc_status_t eipc_deinit(eipc_context_t* ctx){
    if(!ctx){
        return EIPC_ERR_INVAL;
    }

    for (int i = 0; i < EIPC_MAX_CLIENTS; i++) {
        eipc_remove_peer(ctx, &ctx->peer_table.peers[i]);
    }

    if(ctx->self_fd >= 0){
        eipc_poller_del(&ctx->poller, ctx->self_fd);
        close(ctx->self_fd);
        ctx->self_fd = -1;
    }

    if(ctx->role == EIPC_ROLE_BROKER){
        unlink(ctx->socket_path);
    }

    eipc_poller_deinit(&ctx->poller);
    eipc_transite_state(ctx, EIPC_STATE_UNINITIALIZED);
    return EIPC_OK;
}

static void eipc_epoll_handler(eipc_context_t* ctx, struct epoll_event *event){
    int fd = event->data.fd;
    uint32_t events = event->events;

    if(fd == ctx->poller.timer_fd){
        eipc_poller_consume_timer(&ctx->poller);
        eipc_status_t status = eipc_connect(ctx);
        if(status == EIPC_OK){
            eipc_poller_disarm_timer(&ctx->poller);
        }
        
        return;
    }

    if (ctx->role == EIPC_ROLE_BROKER && fd == ctx->self_fd) {
        if (events & EPOLLIN) {
            int client_fd;
            while ((client_fd = eipc_unix_socket_accept(ctx->self_fd)) >= 0) {
                eipc_on_new_connection(ctx, client_fd);
            }
        }
        return;
    }

    if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
        printf("[EIPC] Peer/Broker %d hung up or error (events: 0x%x)\n", fd, events);
        eipc_on_disconnect(ctx, fd);
        return;
    }

    if (events & EPOLLIN) {
        eipc_status_t status = eipc_on_msg(ctx, fd);
        if (status == EIPC_ERR_DISCONNECTED) {
            eipc_on_disconnect(ctx, fd);
        }
    }
}

eipc_status_t eipc_loop(eipc_context_t* ctx, uint32_t timeout_msec){
    if(!ctx || (ctx->state != EIPC_STATE_CONNECTED && ctx->state != EIPC_STATE_DISCONNECTED)){
        return EIPC_ERR_INVAL;
    }

    struct epoll_event events[EIPC_EPOLL_MAX_EVENTS];
    int n_events = eipc_poller_wait(&ctx->poller, events, EIPC_EPOLL_MAX_EVENTS, timeout_msec);
    if (n_events < 0) {
        if (errno == EINTR) return EIPC_OK;
        perror("[EPIC] epoll_wait error");
        return EIPC_ERR_IO;
    }

    for (int i = 0; i < n_events; i++) {
        eipc_epoll_handler(ctx, &events[i]);
    }

    return EIPC_OK;
}

eipc_status_t eipc_create_direct_link(eipc_context_t* ctx, int peer_id1, int  peer_id2){
    if(!ctx || ctx->state != EIPC_STATE_CONNECTED){
        return EIPC_ERR_INVAL;
    }

    if (peer_id1 == peer_id2) {
        return EIPC_ERR_INVAL;
    }

    if(ctx->role != EIPC_ROLE_BROKER){
        return EIPC_ERR_NOT_PERMITTED;
    }

    eipc_peer_t* peer1 = eipc_peer_table_get_by_id(&ctx->peer_table, peer_id1);
    eipc_peer_t* peer2 = eipc_peer_table_get_by_id(&ctx->peer_table, peer_id2);
    if (peer1 == NULL || peer2 == NULL) {
        return EIPC_ERR_INVAL;
    }

    int sv[2];
    if (eipc_unix_socket_pair(sv) != EIPC_OK) {
        return EIPC_ERR_IO;
    }
    eipc_message_t msg1, msg2;
    eipc_message_init(&msg1);
    eipc_message_init(&msg2);

    eipc_message_set_type(&msg1, EIPC_FRAME_TYPE_SERVICE_FD);
    eipc_message_set_type(&msg2, EIPC_FRAME_TYPE_SERVICE_FD);

    eipc_message_set_fds(&msg1, &sv[0], 1);
    eipc_message_set_fds(&msg2, &sv[1], 1);

    eipc_status_t ret1 = eipc_peer_send(peer1, &msg1);
    eipc_status_t ret2 = eipc_peer_send(peer2, &msg2);

    close(sv[0]);
    close(sv[1]);

    if (ret1 != EIPC_OK) return ret1;
    if (ret2 != EIPC_OK) return ret2;
    
    return EIPC_OK;
}

eipc_status_t eipc_send_message(eipc_context_t* ctx, int peer_id, const eipc_message_t *msg){
    return eipc_peer_send(eipc_peer_table_get_by_id(&ctx->peer_table, peer_id), msg);
}
