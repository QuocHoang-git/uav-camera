#ifndef __EIPC_PEER_TABLE_H
#define __EIPC_PEER_TABLE_H

#include "peer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    eipc_peer_t peers[EIPC_MAX_CLIENTS];
    int count;
} eipc_peer_table_t;

void eipc_peer_table_init(eipc_peer_table_t* table);
eipc_peer_t* eipc_peer_table_add(eipc_peer_table_t* table, int fd);
eipc_peer_t* eipc_peer_table_get_by_fd(eipc_peer_table_t* table, int fd);
eipc_peer_t* eipc_peer_table_get_by_id(eipc_peer_table_t* table, int id);
eipc_status_t eipc_peer_table_remove(eipc_peer_table_t* table, int fd);
void eipc_peer_table_clear(eipc_peer_table_t* table);
static inline int eipc_peer_table_count(const eipc_peer_table_t* table) {
    return (table != NULL) ? table->count : 0;
}

#ifdef __cplusplus
}
#endif

#endif // __EIPC_PEER_TABLE_H
