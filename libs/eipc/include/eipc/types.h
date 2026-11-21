#ifndef __EIPC_TYPES_H
#define __EIPC_TYPES_H

#include <stdint.h>
#include "eipc/defines.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    /* success */
    EIPC_OK                  =  0,

    /* System common error */
    EIPC_ERR_GENERIC         = -1,  // Undifined
    EIPC_ERR_INVAL           = -2,  // Invalid argument (NULL pointer, wrong value)
    EIPC_ERR_NOMEM           = -3,  // out of memory (calloc/malloc fail)
    EIPC_ERR_BUSY            = -4,  // Resource busy
    EIPC_ERR_NOT_CONFIG      = -5,  // epic context not set config

    /* Socket / Network / I/O */
    EIPC_ERR_IO              = -10, // socket (bind fail)
    EIPC_ERR_DISCONNECTED    = -11, // Disconnected
    EIPC_ERR_TIMEOUT         = -12, // Timeout
    EIPC_ERR_FULL            = -13, // Full
    EIPC_ERR_IO_BIND         = -14, // socket (bind fail)
    EIPC_ERR_IO_LISTEN       = -15, // socket (listen fail)
    EIPC_ERR_IO_CONNECT      = -16, // socket (connect fail)

    /* IPC internal error */
    EIPC_ERR_NOT_PERMITTED   = -21, // not permit
    EIPC_ERR_NOT_INIT        = -22, // Module uninitialized
} eipc_status_t;

typedef enum {
    EIPC_STATE_INVALID = -1,
    EIPC_STATE_ERROR = 0,
    EIPC_STATE_UNINITIALIZED,
    EIPC_STATE_CONNECTED,
    EIPC_STATE_DISCONNECTED,
} eipc_state_t;

typedef enum {
    EIPC_ROLE_BROKER = 0,
    EIPC_ROLE_CLIENT = 1,
} eipc_role_t;

enum {
    EIPC_FRAME_TYPE_HEARTBEAT       = 0x01,
    EIPC_FRAME_TYPE_SERVICE_FD      = 0x02,

    EIPC_FRAME_TYPE_REQUEST         = 0x20,
    EIPC_FRAME_TYPE_RESPONSE        = 0x21,

    EIPC_FRAME_TYPE_DATA            = 0x31,
};
typedef uint8_t eipc_frame_type_t;

typedef struct {
    uint8_t* data;
    uint32_t size;
    int *fds;
    int num_fds;

    eipc_frame_type_t   type;
} eipc_message_t;

typedef struct eipc_context eipc_context_t;

typedef void (*eipc_connect_cb)(eipc_context_t *ctx, int peer_id, void *userdata);
typedef void (*eipc_disconnect_cb)(eipc_context_t *ctx, int peer_id, void *userdata);
typedef void (*eipc_msg_cb)(eipc_context_t *ctx, int peer_id, const eipc_message_t *data, void *userdata);

typedef struct {
    eipc_connect_cb         on_connect;
    eipc_disconnect_cb      on_disconnect;
    eipc_msg_cb             on_msg;
    void                   *userdata;
} eipc_callbacks_t;

typedef struct {
    const char* socket_path;
    eipc_role_t role;
    eipc_callbacks_t callbacks;
} eipc_config_t;

/**
 * @brief Convert error code to text
 */
const char* eipc_strerror(eipc_status_t status);

#ifdef __cplusplus
}
#endif

#endif // __EIPC_TYPES_H
