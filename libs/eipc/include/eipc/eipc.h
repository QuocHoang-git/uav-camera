#ifndef __EIPC_H
#define __EIPC_H

#include "eipc/types.h"
#include "eipc/message.h"

#ifdef __cplusplus
extern "C" {
#endif

eipc_context_t* eipc_create(eipc_config_t *config);
eipc_status_t eipc_destroy(eipc_context_t* ctx);

eipc_state_t eipc_get_state(eipc_context_t* ctx);
int eipc_get_epollfd(eipc_context_t* ctx);

eipc_status_t eipc_init(eipc_context_t* ctx);
eipc_status_t eipc_deinit(eipc_context_t* ctx);
eipc_status_t eipc_connect(eipc_context_t* ctx);

eipc_status_t eipc_loop(eipc_context_t* ctx, uint32_t timeout_msec);
eipc_status_t eipc_create_direct_link(eipc_context_t* ctx, int peer_id1, int  peer_id2);

eipc_status_t eipc_send_message(eipc_context_t* ctx, int peer_id, const eipc_message_t *msg);

#ifdef __cplusplus
}
#endif

#endif // __EIPC_H
