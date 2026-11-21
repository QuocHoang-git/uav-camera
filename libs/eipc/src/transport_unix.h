#ifndef __EIPC_TRANSPORT_UNIX_H
#define __EIPC_TRANSPORT_UNIX_H

#include "eipc/types.h"

int  eipc_unix_socket_create(void);

int  eipc_unix_socket_listen(const char *path, int backlog);

int  eipc_unix_socket_accept(int server_fd);

int  eipc_unix_socket_connect(const char *path);

eipc_status_t eipc_unix_socket_pair(int sv[2]);

void eipc_unix_socket_close(int *fd);

#endif
