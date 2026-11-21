#define _GNU_SOURCE

#include "transport_unix.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>

int eipc_unix_socket_create(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        perror("[EIPC Transport] socket() failed");
        return -1;
    }
    return fd;
}

void eipc_unix_socket_close(int *fd) {
    if (fd && *fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

int eipc_unix_socket_listen(const char *path, int backlog) {
    if (!path || strlen(path) == 0) {
        return -1;
    }
    struct sockaddr_un addr;
    if (strlen(path) >= sizeof(addr.sun_path)) {
        fprintf(stderr, "[EIPC Transport] Socket path too long: %s\n", path);
        return -1;
    }
    int server_fd = eipc_unix_socket_create();
    if (server_fd < 0) {
        return -1;
    }
    
    unlink(path);
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[EIPC Transport] bind() failed");
        close(server_fd);
        return -1;
    }

    int listen_backlog = (backlog > 0) ? backlog : 16;
    if (listen(server_fd, listen_backlog) < 0) {
        perror("[EIPC Transport] listen() failed");
        close(server_fd);
        unlink(path);
        return -1;
    }
    return server_fd;
}

int eipc_unix_socket_accept(int server_fd) {
    if (server_fd < 0) {
        return -1;
    }
    
    int client_fd = accept4(server_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (client_fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return -1;
        }
        if (errno == EINTR) {
            return -1;
        }
        perror("[EIPC Transport] accept4() failed");
        return -1;
    }
    return client_fd;
}

int eipc_unix_socket_connect(const char *path) {
    if (!path || strlen(path) == 0) {
        return -1;
    }
    struct sockaddr_un addr;
    if (strlen(path) >= sizeof(addr.sun_path)) {
        fprintf(stderr, "[EIPC Transport] Socket path too long: %s\n", path);
        return -1;
    }
    int client_fd = eipc_unix_socket_create();
    if (client_fd < 0) {
        return -1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    if (connect(client_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        if (errno != EINPROGRESS) {
            close(client_fd);
            return -1;
        }
    }
    return client_fd;
}

eipc_status_t eipc_unix_socket_pair(int sv[2]) {
    if (sv == NULL) {
        return EIPC_ERR_INVAL;
    }
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sv) != 0) {
        perror("[EIPC Transport] socketpair() failed");
        return EIPC_ERR_IO;
    }
    return EIPC_OK;
}
