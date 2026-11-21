#include "peer_table.h"

void eipc_peer_table_init(eipc_peer_table_t* table) {
    if (table == NULL) return;
    for (int i = 0; i < EIPC_MAX_CLIENTS; ++i) {
        eipc_peer_init(&table->peers[i]);
    }
    table->count = 0;
}

eipc_peer_t* eipc_peer_table_add(eipc_peer_table_t* table, int fd) {
    if (table == NULL || fd < 0) return NULL;
    
    eipc_peer_t* existing = eipc_peer_table_get_by_fd(table, fd);
    if (existing != NULL) {
        return existing;
    }
    
    for (int i = 0; i < EIPC_MAX_CLIENTS; ++i) {
        if (table->peers[i].fd < 0) {
            eipc_peer_init(&table->peers[i]);
            eipc_peer_set_id(&table->peers[i], fd);
            eipc_peer_set_fd(&table->peers[i], fd);
            table->peers[i].state = EIPC_PEER_STATE_CONNECTED;
            table->count++;
            return &table->peers[i];
        }
    }
    
    return NULL;
}

eipc_peer_t* eipc_peer_table_get_by_fd(eipc_peer_table_t* table, int fd) {
    if (table == NULL || fd < 0) return NULL;
    for (int i = 0; i < EIPC_MAX_CLIENTS; ++i) {
        if (table->peers[i].fd == fd) {
            return &table->peers[i];
        }
    }
    return NULL;
}

eipc_peer_t* eipc_peer_table_get_by_id(eipc_peer_table_t* table, int id) {
    if (table == NULL || id < 0) return NULL;
    for (int i = 0; i < EIPC_MAX_CLIENTS; ++i) {
        if (table->peers[i].id == id) {
            return &table->peers[i];
        }
    }
    return NULL;
}

eipc_status_t eipc_peer_table_remove(eipc_peer_table_t* table, int fd) {
    if (table == NULL || fd < 0) return EIPC_ERR_INVAL;
    for (int i = 0; i < EIPC_MAX_CLIENTS; ++i) {
        if (table->peers[i].fd == fd) {
            eipc_peer_deinit(&table->peers[i]); // Đóng socket và reset sequence
            table->count--;
            return EIPC_OK;
        }
    }
    return EIPC_ERR_INVAL;
}

void eipc_peer_table_clear(eipc_peer_table_t* table) {
    if (table == NULL) return;
    for (int i = 0; i < EIPC_MAX_CLIENTS; ++i) {
        if (table->peers[i].fd >= 0) {
            eipc_peer_deinit(&table->peers[i]);
        }
    }
    table->count = 0;
}
