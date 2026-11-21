#include "eipc/eipc.h"

#include <sys/epoll.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>

typedef struct {
    int client_ids[16];
    eipc_context_t* eipc_ctx;
    int epollfd;
    int sigfd;
    bool is_running;
} app_context_t;

void on_connection(eipc_context_t *eipc_ctx, int client_id, void *userdata){
    (void)eipc_ctx;
    (void)userdata;

    printf("on_connection %d\n", client_id);

    eipc_message_t message;
    eipc_message_init(&message);
    const char* data = "Hello there\n";
    eipc_message_set_data(&message, data, strlen(data) + 1);
    eipc_send_message(eipc_ctx, client_id, &message);
}

void on_disconnect(eipc_context_t *eipc_ctx, int client_id, void *userdata){
    (void)eipc_ctx;
    (void)userdata;
    printf("on_disconnect %d\n", client_id);
}

void on_msg(eipc_context_t *eipc_ctx, int client_id, const eipc_message_t *data, void *userdata){
    (void)eipc_ctx;
    (void)userdata;
    printf("on_msg %d\n", client_id);
    printf("message: %s\n", (char*)data->data);
}

bool app_ctx_init(app_context_t *ctx){
    eipc_config_t client_config = {
        .socket_path = "/tmp/eipc_.sock",
        .role = EIPC_ROLE_CLIENT,
        .callbacks = {
            .on_connect = on_connection,
            .on_disconnect = on_disconnect,
            .on_msg = on_msg,
            .userdata = ctx,
        }
    };

    ctx->is_running = false;
    ctx->eipc_ctx = eipc_create(&client_config);
    if(ctx->eipc_ctx == NULL){
        printf("client cann't create\n");
        return false;
    }

    ctx->epollfd = epoll_create1(0);
    if (ctx->epollfd == -1) {
        perror("epoll_create1");
        return false;
    }

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
        perror("sigprocmask");
        return false;
    }

    ctx->sigfd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (ctx->sigfd == -1) {
        perror("signalfd");
        return false;
    }

    struct epoll_event sig_ev;
    sig_ev.events = EPOLLIN;
    sig_ev.data.fd = ctx->sigfd;
    if (epoll_ctl(ctx->epollfd, EPOLL_CTL_ADD, ctx->sigfd, &sig_ev) == -1) {
        perror("epoll_ctl: sigfd");
        return false;
    }

    eipc_status_t eipc_s = eipc_init(ctx->eipc_ctx);
    if(eipc_s != EIPC_OK){
        printf("client cann't init %s\n", eipc_strerror(eipc_s));
        return false;
    }

    sig_ev.events = EPOLLIN;
    sig_ev.data.fd = eipc_get_epollfd(ctx->eipc_ctx);
    if (epoll_ctl(ctx->epollfd, EPOLL_CTL_ADD, eipc_get_epollfd(ctx->eipc_ctx), &sig_ev) == -1) {
        perror("epoll_ctl: sigfd");
        return false;
    }

    ctx->is_running = true;

    return true;
}

void app_ctx_deinit(app_context_t *ctx){
    eipc_destroy(ctx->eipc_ctx);
    close(ctx->sigfd);
    close(ctx->epollfd);
}

int main(int argc, char* argv[]){
    (void)argc;
    (void)argv;
    app_context_t app_ctx;
    app_ctx_init(&app_ctx);
    printf("EPIC client service start\n");

    const int MAX_EVENTS = 10;
    struct epoll_event events[MAX_EVENTS];

    while(app_ctx.is_running){
        int nfds = epoll_wait(app_ctx.epollfd, events, MAX_EVENTS, -1);
        if(nfds == -1){
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }

        for(int n = 0; n < nfds; ++n){
            if (events[n].data.fd == app_ctx.sigfd) {
                struct signalfd_siginfo fdsi;
                ssize_t s = read(app_ctx.sigfd, &fdsi, sizeof(fdsi));
                if (s == sizeof(fdsi)) {
                    printf( "\n[App 1] Received signal %d (Exit requested). Stopping gracefully...", fdsi.ssi_signo);
                    app_ctx.is_running = false;
                    break;
                }
            }
            else if (events[n].data.fd == eipc_get_epollfd(app_ctx.eipc_ctx))
            {
                eipc_loop(app_ctx.eipc_ctx, 1);
            }
            
        }
    }

    app_ctx_deinit(&app_ctx);
    printf("Exitting....\n");

    return 0;
}