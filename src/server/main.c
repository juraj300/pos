#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "common/ipc.h"

typedef struct {
    int cli_fd;
    volatile int running;
} server_ctx_t;

static int write_all(int fd, const char *buf, size_t len) {
      size_t off = 0;
      while (off < len) {
        ssize_t n = write(fd, buf + off, len - off);
        if (n < 0) return -1;
        off += (size_t)n;
    }
    return 0;
}

static void* tick_thread(void *arg) {
      server_ctx_t *ctx = (server_ctx_t*)arg;

      for (int tick = 1; tick <= 100 && ctx->running; tick++) {
                   char out[64];
                   int n = snprintf(out, sizeof(out), "%s%d\n", MSG_STATE_PREFIX, tick);
                   if (n < 0) break;

        if (write_all(ctx->cli_fd, out, (size_t)n) < 0) {
                       perror("server write (STATE)");
            ctx->running = 0;
            break;
                   }

        printf("server: sent: %s", out);
        usleep(200000);
    }

    ctx->running = 0;
    return NULL;
}

static void* recv_thread(void *arg) {
      server_ctx_t *ctx = (server_ctx_t*)arg;

      char buf[256];
      while (ctx->running) {
        ssize_t n = read(ctx->cli_fd, buf, sizeof(buf) - 1);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("server read");
            ctx->running = 0;
            break;
        }
        if (n == 0) {
            printf("server: client closed connection\n");
            ctx->running = 0;
            break;
        }

        buf[n] = '\0';
        printf("server: received chunk:\n%s", buf);

        if (strstr(buf, "INPUT q") != NULL) {
            printf("server: got quit input\n");
            ctx->running = 0;
            break;
        }
    }

    return NULL;
}

int main(void) {
      int srv_fd = socket(AF_UNIX, SOCK_STREAM, 0);
      if (srv_fd < 0) { perror("socket"); return 1; }

    struct sockaddr_un addr;
      memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, POS_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    unlink(POS_SOCKET_PATH);

    if (bind(srv_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(srv_fd);
        return 1;
    }

    if (listen(srv_fd, 1) < 0) {
        perror("listen");
        close(srv_fd);
        return 1;
    }

    printf("server: listening on %s\n", POS_SOCKET_PATH);

    int cli_fd = accept(srv_fd, NULL, NULL);
      if (cli_fd < 0) {
        perror("accept");
        close(srv_fd);
        unlink(POS_SOCKET_PATH);
        return 1;
    }

    char inbuf[128] = {0};
      ssize_t rn = read(cli_fd, inbuf, sizeof(inbuf) - 1);
      if (rn < 0) {
        perror("read");
        close(cli_fd);
        close(srv_fd);
        unlink(POS_SOCKET_PATH);
        return 1;
    }
    printf("server: received: %s", inbuf);

    if (strcmp(inbuf, MSG_PING) == 0) {
        if (write_all(cli_fd, MSG_PONG, strlen(MSG_PONG)) < 0) perror("write");
        printf("server: sent: %s", MSG_PONG);
    } else {
        printf("server: expected PING, got something else\n");
    }

    server_ctx_t ctx;
      ctx.cli_fd = cli_fd;
    ctx.running = 1;

    pthread_t t_tick, t_recv;
      if (pthread_create(&t_tick, NULL, tick_thread, &ctx) != 0) {
        perror("pthread_create tick");
        ctx.running = 0;
    }
    if (pthread_create(&t_recv, NULL, recv_thread, &ctx) != 0) {
        perror("pthread_create recv");
        ctx.running = 0;
    }

    pthread_join(t_tick, NULL);
    pthread_join(t_recv, NULL);

    close(cli_fd);
    close(srv_fd);
    unlink(POS_SOCKET_PATH);
    return 0;
}

