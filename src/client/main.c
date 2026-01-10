#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "common/ipc.h"

typedef struct {
    int fd;
    volatile int running;
} client_ctx_t;

static int write_all(int fd, const char *buf, size_t len) {
      size_t off = 0;
      while (off < len) {
        ssize_t n = write(fd, buf + off, len - off);
        if (n < 0) return -1;
        off += (size_t)n;
    }
    return 0;
}

static void* recv_thread(void *arg) {
      client_ctx_t *ctx = (client_ctx_t*)arg;
      char buf[256];

      while (ctx->running) {
        ssize_t n = read(ctx->fd, buf, sizeof(buf) - 1);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("client read");
            ctx->running = 0;
            break;
        }
        if (n == 0) {
            printf("client: server closed connection\n");
            ctx->running = 0;
            break;
        }
        buf[n] = '\0';
        printf("client: received chunk:\n%s", buf);
    }
    return NULL;
}

static void* input_thread(void *arg) {
      client_ctx_t *ctx = (client_ctx_t*)arg;

      printf("client: type keys, press 'q' then Enter to quit\n");

    char line[64];
      while (ctx->running && fgets(line, sizeof(line), stdin) != NULL) {
        char c = line[0];
        if (c == '\n' || c == '\0') continue;

        char msg[64];
        int n = snprintf(msg, sizeof(msg), "%s%c\n", MSG_INPUT_PREFIX, c);
        if (n < 0) continue;

        if (write_all(ctx->fd, msg, (size_t)n) < 0) {
            perror("client write (INPUT)");
            ctx->running = 0;
            break;
        }

        if (c == 'q') {
            ctx->running = 0;
            break;
        }
    }

    return NULL;
}

int main(void) {
      int fd = socket(AF_UNIX, SOCK_STREAM, 0);
      if (fd < 0) { perror("socket"); return 1; }

    struct sockaddr_un addr;
      memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, POS_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return 1;
    }

    if (write_all(fd, MSG_PING, strlen(MSG_PING)) < 0) {
        perror("write");
        close(fd);
        return 1;
    }
    printf("client: sent: %s", MSG_PING);

    client_ctx_t ctx;
      ctx.fd = fd;
    ctx.running = 1;

    pthread_t t_recv, t_in;
      if (pthread_create(&t_recv, NULL, recv_thread, &ctx) != 0) {
        perror("pthread_create recv");
        close(fd);
        return 1;
    }
    if (pthread_create(&t_in, NULL, input_thread, &ctx) != 0) {
        perror("pthread_create input");
        ctx.running = 0;
    }

    pthread_join(t_in, NULL);
    ctx.running = 0;

    shutdown(fd, SHUT_RDWR);
    pthread_join(t_recv, NULL);

    close(fd);
    return 0;
}

