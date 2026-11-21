#ifndef __EIPC_DEFINES_H
#define __EIPC_DEFINES_H

#ifdef __cplusplus
extern "C" {
#endif

#define EIPC_MAGIC_NUMBER       (0x4550) // "EP" trong mã ASCII
#define EIPC_VERSION_MAJOR      (1)
#define EIPC_VERSION_MINOR      (0)
#define EIPC_VERSION_PATCH      (0)

#ifndef EIPC_MAX_FD_TRANSFER
#define EIPC_MAX_FD_TRANSFER    (4)
#endif

#ifndef EIPC_MAX_CLIENTS
#define EIPC_MAX_CLIENTS (16)
#endif

#ifndef EIPC_EPOLL_MAX_EVENTS
#define EIPC_EPOLL_MAX_EVENTS (10)
#endif

#define EIPC_MAX_PAYLOAD_SIZE   (1024) // 4 KB

#ifdef __cplusplus
}
#endif

#endif // __EIPC_DEFINES_H
