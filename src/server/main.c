#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "common/ipc.h"

typedef enum { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT } dir_t;

typedef struct {
    int cli_fd;
    volatile int running;

    pthread_mutex_t lock;
    int sx[3];
    int sy[3];
    // int x, y;
    dir_t dir;
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

static char dir_to_char(dir_t d) {
      switch (d) {
        case DIR_UP: return 'U';
          case DIR_DOWN: return 'D';
          case DIR_LEFT: return 'L';
          case DIR_RIGHT: return 'R';
      }
    return 'R';
}

static void step_pos(int *x, int *y, dir_t d) {
      if (d == DIR_UP) (*y)--;
      else if (d == DIR_DOWN) (*y)++;
      else if (d == DIR_LEFT) (*x)--;
      else if (d == DIR_RIGHT) (*x)++;

      if (*x < 0) *x = GRID_W - 1;
      if (*x >= GRID_W) *x = 0;
      if (*y < 0) *y = GRID_H - 1;
      if (*y >= GRID_H) *y = 0;
}

static void* tick_thread(void *arg) {
      server_ctx_t *ctx = (server_ctx_t*)arg;

      for (int tick = 1; tick <= 1000000 && ctx->running; tick++) {
                   //int x, y;
                   dir_t d;

                   pthread_mutex_lock(&ctx->lock);
        //step_pos(&ctx->x, &ctx->y, ctx->dir);
        //x = ctx->x;
        //y = ctx->y;
        int nx = ctx->sx[0];
        int ny = ctx->sy[0];
        step_pos(&nx, &ny, ctx->dir);

        for (int i = 2; i >= 1; i--) {
            ctx->sx[i] = ctx->sx[i - 1];
            ctx->sy[i] = ctx->sy[i - 1];
        }

        ctx->sx[0] = nx;
        ctx->sy[0] = ny;


        int x0 = ctx->sx[0], y0 = ctx->sy[0];
        int x1 = ctx->sx[1], y1 = ctx->sy[1];
        int x2 = ctx->sx[2], y2 = ctx->sy[2];
        d = ctx->dir;

        if ((ctx->sx[0] == ctx->sx[1] && ctx->sy[0] == ctx->sy[1]) ||
           (ctx->sx[0] == ctx->sx[2] && ctx->sy[0] == ctx->sy[2])) {
           ctx->running = 0;
        }
        pthread_mutex_unlock(&ctx->lock);

        if (!ctx->running) {
          char go[64];
          int gn = snprintf(go, sizeof(go), "GAMEOVER SELF %d\n", tick);
          if (gn > 0) {
                   (void)write_all(ctx->cli_fd, go, (size_t)gn);
               }
          break;
        }
        char out[128];
        int n = snprintf(out, sizeof(out),
                            "STATE %d %d %d %d %d %d %c %d\n",
                            x0, y0, x1, y1, x2, y2, dir_to_char(d), tick);
        if (n < 0) break;

        if (write_all(ctx->cli_fd, out, (size_t)n) < 0) {
                       perror("server write (STATE)");
            ctx->running = 0;
            break;
                   }

        usleep(200000);
    }

    ctx->running = 0;
    return NULL;
}


static int is_opposite(dir_t a, dir_t b) {
      return (a == DIR_UP && b == DIR_DOWN) ||
             (a == DIR_DOWN && b == DIR_UP) ||
             (a == DIR_LEFT && b == DIR_RIGHT) ||
             (a == DIR_RIGHT && b == DIR_LEFT);
}


static void set_dir_from_input(server_ctx_t *ctx, char c) {
      dir_t ndir;
      int ok = 1;

      if (c == 'w') ndir = DIR_UP;
      else if (c == 's') ndir = DIR_DOWN;
      else if (c == 'a') ndir = DIR_LEFT;
      else if (c == 'd') ndir = DIR_RIGHT;
      else ok = 0;

      if (!ok) return;

      pthread_mutex_lock(&ctx->lock);
      if (!is_opposite(ctx->dir, ndir)) {
        ctx->dir = ndir;
    }
      pthread_mutex_unlock(&ctx->lock);
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

        char *p = buf;
        while ((p = strstr(p, "INPUT ")) != NULL) {
            char c = p[6];
            if (c == 'q') {
                printf("server: got quit input\n");
                ctx->running = 0;
                break;
            }
            set_dir_from_input(ctx, c);
            p += 6;
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
    }

    server_ctx_t ctx;
      ctx.cli_fd = cli_fd;
    ctx.running = 1;
    pthread_mutex_init(&ctx.lock, NULL);
    //ctx.x = GRID_W / 2;
    //ctx.y = GRID_H / 2;
    //ctx.dir = DIR_RIGHT;
    int cx = GRID_W / 2;
    int cy = GRID_H / 2;

    ctx.sx[0] = cx;     ctx.sy[0] = cy;
    ctx.sx[1] = cx - 1; ctx.sy[1] = cy;
    ctx.sx[2] = cx - 2; ctx.sy[2] = cy;
    ctx.dir = DIR_RIGHT;

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

    pthread_mutex_destroy(&ctx.lock);

    close(cli_fd);
    close(srv_fd);
    unlink(POS_SOCKET_PATH);
    return 0;
}

