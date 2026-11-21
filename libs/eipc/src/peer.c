#include "peer.h"
#include "frame.h"
#include "eipc/message.h"

#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

void eipc_peer_init(eipc_peer_t* peer){
    if(peer == NULL){
        return;
    }
    peer->fd = -1;
    peer->id = -1;
    peer->sequence_send = 0;
    peer->sequence_receive = 0;
    peer->state = EIPC_PEER_STATE_INIT;
}

void eipc_peer_deinit(eipc_peer_t* peer){
    if(peer == NULL){
        return;
    }

    if(peer->fd >= 0){
        close(peer->fd);
        peer->fd = -1;
    }

    peer->sequence_send = 0;
    peer->sequence_receive = 0;
    peer->state = EIPC_PEER_STATE_INIT;
}

eipc_seq_check_t eipc_peer_update_recv_seq(eipc_peer_t* peer, 
                                                         uint32_t incoming_seq, 
                                                         uint32_t* out_loss_count) {
    if (peer == NULL) {
        return EIPC_SEQ_DUPLICATE;
    }
    if (out_loss_count) {
        *out_loss_count = 0;
    }
    
    if (peer->sequence_receive == 0) {
        peer->sequence_receive = incoming_seq;
        return EIPC_SEQ_FIRST_PACKET;
    }
    uint32_t expected_seq = peer->sequence_receive + 1;
    
    if (incoming_seq == expected_seq) {
        peer->sequence_receive = incoming_seq;
        return EIPC_SEQ_OK;
    }
    
    if (incoming_seq > expected_seq) {
        if (out_loss_count) {
            *out_loss_count = incoming_seq - expected_seq;
        }
        peer->sequence_receive = incoming_seq;
        return EIPC_SEQ_PACKET_LOSS;
    }
    
    return EIPC_SEQ_DUPLICATE;
}

eipc_status_t eipc_peer_send(eipc_peer_t *peer, const eipc_message_t *msg) {
    if (peer == NULL || msg == NULL) {
        return EIPC_ERR_INVAL;
    }
    if (peer->fd < 0 || peer->state != EIPC_PEER_STATE_CONNECTED) {
        return EIPC_ERR_DISCONNECTED;
    }
    if (msg->size > 0 && msg->data == NULL) {
        return EIPC_ERR_INVAL;
    }
    eipc_frame_header_t header;
    eipc_frame_header_init(&header);
    header.type = msg->type;
    header.payload_size = msg->size;
    header.sequence = eipc_peer_next_send_seq(peer);
    if (msg->fds != NULL && msg->num_fds > 0) {
        eipc_frame_flags_set(&header, EIPC_FLAG_HAS_FD_MASK);
    }
    if (msg->size > 0) {
        eipc_frame_header_add_crc(&header, msg->data);
    }
    struct iovec iov[2];
    iov[0].iov_base = (void *)&header;
    iov[0].iov_len  = sizeof(eipc_frame_header_t);
    int iov_count = 1;
    if (msg->size > 0) {
        iov[1].iov_base = (void *)msg->data;
        iov[1].iov_len  = msg->size;
        iov_count = 2;
    }
    struct msghdr socket_msg;
    memset(&socket_msg, 0, sizeof(socket_msg));
    socket_msg.msg_iov    = iov;
    socket_msg.msg_iovlen = iov_count;
    char cmsg_buf[CMSG_SPACE(EIPC_MAX_FD_TRANSFER * sizeof(int))];
    if (msg->fds != NULL && msg->num_fds > 0) {
        int send_fds_count = (msg->num_fds > EIPC_MAX_FD_TRANSFER) ? EIPC_MAX_FD_TRANSFER : msg->num_fds;
        size_t fds_size = send_fds_count * sizeof(int);
        memset(cmsg_buf, 0, sizeof(cmsg_buf));
        socket_msg.msg_control    = cmsg_buf;
        socket_msg.msg_controllen = CMSG_SPACE(fds_size);
        
        struct cmsghdr *cmsg = CMSG_FIRSTHDR(&socket_msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type  = SCM_RIGHTS;
        cmsg->cmsg_len   = CMSG_LEN(fds_size);
        memcpy(CMSG_DATA(cmsg), msg->fds, fds_size);
    }
    ssize_t total_expected = sizeof(eipc_frame_header_t) + msg->size;
    ssize_t sent_bytes = sendmsg(peer->fd, &socket_msg, MSG_NOSIGNAL);
    if (sent_bytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return EIPC_ERR_BUSY;
        }
        if (errno == EPIPE || errno == ECONNRESET) {
            peer->state = EIPC_PEER_STATE_DISCONNECTED;
            return EIPC_ERR_DISCONNECTED;
        }
        return EIPC_ERR_IO;
    }
    if (sent_bytes != total_expected) {
        return EIPC_ERR_IO;
    }
    return EIPC_OK;
}

eipc_status_t eipc_peer_receive(eipc_peer_t *peer, 
                                uint8_t *rx_buf, size_t rx_buf_size,
                                int *rx_fds, size_t max_fds,
                                eipc_message_t *msg) {
    if (peer == NULL || rx_buf == NULL || msg == NULL) {
        return EIPC_ERR_INVAL;
    }
    if (peer->fd < 0 || peer->state != EIPC_PEER_STATE_CONNECTED) {
        return EIPC_ERR_DISCONNECTED;
    }
    struct iovec iov;
    iov.iov_base = rx_buf;
    iov.iov_len  = rx_buf_size;
    
    char cmsg_buf[CMSG_SPACE(EIPC_MAX_FD_TRANSFER * sizeof(int))];
    memset(cmsg_buf, 0, sizeof(cmsg_buf));
    
    struct msghdr socket_msg;
    memset(&socket_msg, 0, sizeof(socket_msg));
    socket_msg.msg_iov        = &iov;
    socket_msg.msg_iovlen     = 1;
    socket_msg.msg_control    = cmsg_buf;
    socket_msg.msg_controllen = sizeof(cmsg_buf);
    
    ssize_t bytes_read = recvmsg(peer->fd, &socket_msg, 0);
    if (bytes_read == 0) {
        peer->state = EIPC_PEER_STATE_DISCONNECTED;
        return EIPC_ERR_DISCONNECTED;
    }
    if (bytes_read < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return EIPC_ERR_BUSY;
        }
        if (errno == ECONNRESET || errno == EPIPE) {
            peer->state = EIPC_PEER_STATE_DISCONNECTED;
            return EIPC_ERR_DISCONNECTED;
        }
        return EIPC_ERR_IO;
    }
    
    if (bytes_read < (ssize_t)sizeof(eipc_frame_header_t)) {
        return EIPC_ERR_IO;
    }
    const eipc_frame_header_t *hdr = (const eipc_frame_header_t *)rx_buf;
    const uint8_t *payload_ptr = (hdr->payload_size > 0) ? 
                                 (rx_buf + sizeof(eipc_frame_header_t)) : NULL;
    if (!eipc_frame_is_valid(hdr, payload_ptr)) {
        return EIPC_ERR_INVAL;
    }
    
    ssize_t expected_total = sizeof(eipc_frame_header_t) + hdr->payload_size;
    if (bytes_read != expected_total) {
        return EIPC_ERR_IO;
    }
    
    uint32_t lost_packets = 0;
    eipc_peer_update_recv_seq(peer, hdr->sequence, &lost_packets);
    
    int num_received_fds = 0;
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&socket_msg);
    if (cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
        size_t fds_data_len = cmsg->cmsg_len - CMSG_LEN(0);
        num_received_fds = fds_data_len / sizeof(int);
        if (max_fds > 0 && rx_fds != NULL) {
            if ((size_t)num_received_fds > max_fds) {
                num_received_fds = max_fds;
            }
            memcpy(rx_fds, CMSG_DATA(cmsg), num_received_fds * sizeof(int));
        }
    }
    
    eipc_message_init(msg);
    eipc_message_set_data(msg, (void *)payload_ptr, hdr->payload_size);
    eipc_message_set_type(msg, hdr->type);
    if (num_received_fds > 0 && rx_fds != NULL) {
        eipc_message_set_fds(msg, rx_fds, num_received_fds);
    }
    return EIPC_OK;
}
