#include "eipc/message.h"

void eipc_message_init(eipc_message_t *msg) {
    if (msg == NULL) return;
    
    msg->data = NULL;
    msg->size = 0;
    msg->fds = NULL;
    msg->num_fds = 0;
    msg->type = EIPC_FRAME_TYPE_DATA;
}

void eipc_message_set_data(eipc_message_t *msg, void *data, uint32_t size) {
    if (msg == NULL) return;
    msg->data = (uint8_t*)data;
    msg->size = size;
}

void eipc_message_set_fds(eipc_message_t *msg, int *fds, int num_fds) {
    if (msg == NULL) return;
    msg->fds = fds;
    msg->num_fds = (num_fds > 0) ? num_fds : 0;
}

void eipc_message_set_type(eipc_message_t *msg, eipc_frame_type_t type) {
    if (msg == NULL) return;
    msg->type = type;
}

bool eipc_message_is_valid(const eipc_message_t *msg) {
    if (msg == NULL) return false;
    if (msg->size > 0 && msg->data == NULL) {
        return false;
    }
    if (msg->size > EIPC_MAX_PAYLOAD_SIZE) {
        return false;
    }
    if (msg->num_fds > 0) {
        if (msg->fds == NULL || msg->num_fds > EIPC_MAX_FD_TRANSFER) {
            return false;
        }
    }
    return true;
}
