#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "common/ipc.h"

#define MAX_SNAKE 100

typedef struct {
    int fd;
    volatile int running;

    pthread_mutex_t lock;
    //int x, y, tick;
    int sx[MAX_SNAKE];
    int sy[MAX_SNAKE];
    int len;

    int fx, fy;
    int score;
    int tick;
    char dir;
    int obstacles[GRID_H][GRID_W];
    int time_left;
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

static int load_map(client_ctx_t *ctx, const char *path) {
      FILE *f = fopen(path, "r");
      if (!f) return -1;

      for (int y = 0; y < GRID_H; y++) {
                   for (int x = 0; x < GRID_W; x++) {
                       int c = fgetc(f);
                       while (c == '\r') c = fgetc(f);
                       if (c == EOF) { fclose(f); return -1; }

                       ctx->obstacles[y][x] = (c == '#') ? 1 : 0;
                   }

                   int c = fgetc(f);
                   while (c != '\n' && c != EOF) c = fgetc(f);
               }

      fclose(f);
      return 0;
}


static void render(const client_ctx_t *ctx) {
      printf("\033[H\033[J");

    for (int y = 0; y < GRID_H; y++) {
                 for (int x = 0; x < GRID_W; x++) {
                     //if (x == ctx->x && y == ctx->y) putchar('O');
                       if (x == ctx->sx[0] && y == ctx->sy[0]) putchar('O');
                       else {
                        int body = 0;
                        for (int i = 1; i < ctx->len; i++) {
                          if (x == ctx->sx[i] && y == ctx->sy[i]) {
                            body = 1;
                            break;
                          }
                        }
                        if (body) putchar('o');
                        else if (x == ctx->fx && y == ctx->fy) putchar('*');
                        else if (ctx->obstacles[y][x]) putchar('#');
                        else putchar('.');

                      }
        }
        putchar('\n');
    }
    printf("tick=%d dir=%c  (w/a/s/d + Enter, q + Enter quits)\n", ctx->tick, ctx->dir);
    printf("score=%d tick=%d\n", ctx->score, ctx->tick);
    if (ctx->time_left >= 0) printf("time_left=%d s\n", ctx->time_left);
    else printf("time_left=∞\n");


    fflush(stdout);
}

static void handle_line(client_ctx_t *ctx, const char *line) {
      if (strncmp(line, "GAMEOVER ", 9) == 0) {
          printf("\n=== %s ===\n", line);
          ctx->running = 0;
          return;
      }

      int len, score, tick, time_left;
      int off = 0;

      if (sscanf(line, "STATE %d %d %d %d %n", &len, &score, &tick, &time_left, &off) == 4) {

        pthread_mutex_lock(&ctx->lock);
        ctx->len = len;
        ctx->score = score;
        ctx->tick = tick;
        ctx->time_left = time_left;


        const char *p = line + off;
        for (int i = 0; i < len; i++) {
              sscanf(p, "%d %d %n", &ctx->sx[i], &ctx->sy[i], &off);
              p += off;
          }

        sscanf(p, "%d %d", &ctx->fx, &ctx->fy);

        client_ctx_t snap = *ctx;
        pthread_mutex_unlock(&ctx->lock);

        render(&snap);
      }
    }


static void* recv_thread(void *arg) {
      client_ctx_t *ctx = (client_ctx_t*)arg;

      char buf[256];
      char acc[1024];
      size_t acc_len = 0;

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

        size_t bn = (size_t)n;
        if (acc_len + bn >= sizeof(acc)) {
            acc_len = 0;
        }
        memcpy(acc + acc_len, buf, bn);
        acc_len += bn;
        acc[acc_len] = '\0';

        char *start = acc;
        while (1) {
            char *nl = strchr(start, '\n');
            if (!nl) break;
            *nl = '\0';
            handle_line(ctx, start);
            if (!ctx->running) break;
            start = nl + 1;
        }

        size_t rem = strlen(start);
        memmove(acc, start, rem);
        acc_len = rem;
        acc[acc_len] = '\0';
    }

    return NULL;
}

static void* input_thread(void *arg) {
      client_ctx_t *ctx = (client_ctx_t*)arg;

      char line[64];
      while (ctx->running && fgets(line, sizeof(line), stdin) != NULL) {
        char c = line[0];
        if (c == '\n' || c == '\0') continue;

        char msg[64];
        int n = snprintf(msg, sizeof(msg), "INPUT %c\n", c);
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

int main(int argc, char **argv) {
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

    client_ctx_t ctx;
      ctx.fd = fd;
    ctx.running = 1;
    pthread_mutex_init(&ctx.lock, NULL);
    memset(ctx.obstacles, 0, sizeof(ctx.obstacles));

    if (argc >= 2) {
        if (load_map(&ctx, argv[1]) != 0) {
          fprintf(stderr, "client: failed to load map: %s\n", argv[1]);
          return 1;
      }
    }

    //ctx.x = 0;
    //ctx.y = 0;
    ctx.sx[0]=0; ctx.sy[0]=0;
    ctx.sx[1]=0; ctx.sy[1]=0;
    ctx.sx[2]=0; ctx.sy[2]=0;

    ctx.tick = 0;
    ctx.dir = 'R';

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

    pthread_mutex_destroy(&ctx.lock);

    close(fd);
    return 0;
}

