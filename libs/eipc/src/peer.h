#ifndef __EIPC_PEER_H
#define __EIPC_PEER_H

#include "eipc/types.h"
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum{
    EIPC_PEER_STATE_ERROR = -1,
    EIPC_PEER_STATE_INIT = 0,
    EIPC_PEER_STATE_CONNECTING = 1,
    EIPC_PEER_STATE_CONNECTED = 2,
    EIPC_PEER_STATE_DISCONNECTED = 3,
} eipc_peer_state_t;

typedef enum {
    EIPC_SEQ_OK          = 0,
    EIPC_SEQ_FIRST_PACKET= 1,
    EIPC_SEQ_PACKET_LOSS = 2,
    EIPC_SEQ_DUPLICATE   = 3,
} eipc_seq_check_t;

typedef struct{
    int fd;
    int id;
    
    uint32_t sequence_send;
    uint32_t sequence_receive;

    eipc_peer_state_t state;
} eipc_peer_t;

static inline eipc_peer_state_t eipc_peer_get_state(eipc_peer_t* peer){
    if(peer == NULL){
        return EIPC_PEER_STATE_ERROR;
    }
    return peer->state;
}

static inline uint32_t eipc_peer_next_send_seq(eipc_peer_t* peer) {
    if (peer == NULL) {
        return 0;
    }
    return peer->sequence_send++;
}

static inline uint32_t eipc_peer_get_send_seq(const eipc_peer_t* peer) {
    return (peer != NULL) ? peer->sequence_send : 0;
}

static inline uint32_t eipc_peer_get_recv_seq(const eipc_peer_t* peer) {
    return (peer != NULL) ? peer->sequence_receive : 0;
}

static inline void eipc_peer_reset_seq(eipc_peer_t* peer) {
    if (peer == NULL) return;
    peer->sequence_send = 0;
    peer->sequence_receive = 0;
}

static inline void eipc_peer_set_id(eipc_peer_t* peer, int id){
    if (peer == NULL) return;
    peer->id = id;
}

static inline void eipc_peer_set_fd(eipc_peer_t* peer, int fd){
    if (peer == NULL) return;
    peer->fd = fd;
}

void eipc_peer_init(eipc_peer_t* peer);
void eipc_peer_deinit(eipc_peer_t* peer);

eipc_seq_check_t eipc_peer_update_recv_seq(eipc_peer_t* peer, 
                                                         uint32_t incoming_seq, 
                                                         uint32_t* out_loss_count);

eipc_status_t eipc_peer_send(eipc_peer_t *peer, const eipc_message_t *msg);
eipc_status_t eipc_peer_receive(eipc_peer_t *peer, 
                                uint8_t *rx_buf, size_t rx_buf_size,
                                int *rx_fds, size_t max_fds,
                                eipc_message_t *msg);

#ifdef __cplusplus
}
#endif

#endif // __EIPC_PEER_H
