#include "eipc/types.h"

const char* eipc_strerror(eipc_status_t status) {
    switch (status) {
        case EIPC_OK:                  return "Success";
        case EIPC_ERR_GENERIC:         return "Generic error";
        case EIPC_ERR_INVAL:           return "Invalid argument";
        case EIPC_ERR_NOMEM:           return "Out of memory";
        case EIPC_ERR_BUSY:            return "Resource busy";
        case EIPC_ERR_NOT_CONFIG:      return "Not set config";
        case EIPC_ERR_IO:              return "I/O or socket error";
        case EIPC_ERR_IO_BIND:         return "socket bind error";
        case EIPC_ERR_IO_LISTEN:       return "socket listen error";
        case EIPC_ERR_IO_CONNECT:      return "socket connect error";
        case EIPC_ERR_DISCONNECTED:    return "Peer disconnected";
        case EIPC_ERR_TIMEOUT:         return "Operation timed out";
        case EIPC_ERR_FULL:            return "Queue/Client table full";
        case EIPC_ERR_NOT_INIT:        return "Module uninitialized";
        default:                       return "Unknown error";
    }
}
