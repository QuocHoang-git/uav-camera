#ifndef __EIPC_MESSAGE_H
#define __EIPC_MESSAGE_H

#include "eipc/types.h"
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

void eipc_message_init(eipc_message_t *msg);
void eipc_message_set_data(eipc_message_t *msg, void *data, uint32_t size);
void eipc_message_set_fds(eipc_message_t *msg, int *fds, int num_fds);
void eipc_message_set_type(eipc_message_t *msg, eipc_frame_type_t type);
bool eipc_message_is_valid(const eipc_message_t *msg);

static inline bool eipc_message_has_fds(const eipc_message_t *msg) {
    return (msg != NULL) && (msg->fds != NULL) && (msg->num_fds > 0);
}

#ifdef __cplusplus
}
#endif

#endif // __EIPC_MESSAGE_H