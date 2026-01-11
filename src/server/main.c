#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <sys/select.h>

#include <time.h>

#include <stdlib.h>

#include "common/util.h"
#include "common/map.h"

#include "common/ipc.h"

#define MAX_SNAKE 100

typedef enum { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT } dir_t;

typedef struct {
    int cli_fd;
    volatile int running;

    pthread_mutex_t lock;
    

    int sx[MAX_SNAKE];
    int sy[MAX_SNAKE];
    int len;        

    int fx, fy;     
    int score;

    int obstacles[GRID_H][GRID_W];
    // int x, y;
    dir_t dir;

    int time_limit_sec;
    time_t start_time;
    int paused;
    time_t resume_unfreeze_at;
} server_ctx_t;


static int cell_occupied_by_snake(const server_ctx_t *ctx, int x, int y) {
      for (int i = 0; i < ctx->len; i++) {
                   if (ctx->sx[i] == x && ctx->sy[i] == y) return 1;
               }
      return 0;
}

static void spawn_fruit(server_ctx_t *ctx) {
      while (1) {
        int x = rand() % GRID_W;
        int y = rand() % GRID_H;

        if (ctx->obstacles[y][x]) continue;
        if (cell_occupied_by_snake(ctx, x, y)) continue;

        ctx->fx = x;
        ctx->fy = y;
        return;
    }
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
                   dir_t d;

                   int gameover_wall = 0;
                   int gameover_time = 0;

                   int time_left = -1; 
                   if (ctx->time_limit_sec > 0) {
                       time_t now = time(NULL);
                       int elapsed = (int)difftime(now, ctx->start_time);
                       time_left = ctx->time_limit_sec - elapsed;
                       if (time_left <= 0) {
                           ctx->running = 0;
                           gameover_time = 1;
                       }
                   }

                   pthread_mutex_lock(&ctx->lock);

                   int paused = ctx->paused;
                   int freeze_left = 0;
                   time_t now2 = time(NULL);
                   if (!paused && ctx->resume_unfreeze_at != 0 && now2 < ctx->resume_unfreeze_at) {
                       freeze_left = (int)(ctx->resume_unfreeze_at - now2);
                   }

                   int do_move = 1;
                   if (paused) do_move = 0;
                   if (!paused && freeze_left > 0) do_move = 0;

                   if (do_move) {
                       int nx = ctx->sx[0];
                       int ny = ctx->sy[0];
                       step_pos(&nx, &ny, ctx->dir);

                       if (ctx->obstacles[ny][nx]) {
                           ctx->running = 0;
                           gameover_wall = 1;
                       } else {

                           for (int i = ctx->len - 1; i >= 1; i--) {
                               ctx->sx[i] = ctx->sx[i - 1];
                               ctx->sy[i] = ctx->sy[i - 1];
                           }
                           ctx->sx[0] = nx;
                           ctx->sy[0] = ny;

                           if (ctx->sx[0] == ctx->fx && ctx->sy[0] == ctx->fy) {
                               ctx->score++;

                               if (ctx->len < MAX_SNAKE) {
                                   ctx->sx[ctx->len] = ctx->sx[ctx->len - 1];
                                   ctx->sy[ctx->len] = ctx->sy[ctx->len - 1];
                                   ctx->len++;
                               }

                               spawn_fruit(ctx);
                           }

                           for (int i = 1; i < ctx->len; i++) {
                               if (ctx->sx[0] == ctx->sx[i] && ctx->sy[0] == ctx->sy[i]) {
                                   ctx->running = 0;
                                   break;
                               }
                           }
                       }
                   }

                   d = ctx->dir;
                   (void)d;

                   pthread_mutex_unlock(&ctx->lock);

                   if (!ctx->running) {
                       char go[64];
                       const char *reason = "SELF";
                       if (gameover_time) reason = "TIME";
                       else if (gameover_wall) reason = "WALL";

                       int gn = snprintf(go, sizeof(go), "GAMEOVER %s %d\n", reason, tick);
                       if (gn > 0) {
                           (void)write_all(ctx->cli_fd, go, (size_t)gn);
                       }
                       break;
                   }

                   char out[2048];

                   int n = snprintf(out, sizeof(out), "STATE %d %d %d %d %d %d ",
                                                             ctx->len, ctx->score, tick, time_left, paused, freeze_left);
                   if (n < 0) break;

                   for (int i = 0; i < ctx->len; i++) {
                       int m = snprintf(out + n, sizeof(out) - (size_t)n, "%d %d ",
                                                                     ctx->sx[i], ctx->sy[i]);
                       if (m < 0) { n = -1; break; }
                       n += m;

                       if ((size_t)n >= sizeof(out) - 32) { n = -1; break; }
                   }
                   if (n < 0) break;

                   int m = snprintf(out + n, sizeof(out) - (size_t)n, "%d %d\n", ctx->fx, ctx->fy);
                   if (m < 0) break;
                   n += m;

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
      if (ctx->paused) {
        pthread_mutex_unlock(&ctx->lock);
        return;
      }

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
            if (c == 'p') {
              pthread_mutex_lock(&ctx->lock);
             if (!ctx->paused) {
               ctx->paused = 1;

           } else {
              ctx->paused = 0;
              ctx->resume_unfreeze_at = time(NULL) + 3;
            }
            pthread_mutex_unlock(&ctx->lock);
            p += 6;
            continue;
      }
            set_dir_from_input(ctx, c);
            p += 6;
        }
    }

    return NULL;
}


int main(int argc, char **argv) {
      srand((unsigned)time(NULL));

    int seconds = 0;

      enum { MODE_TIME, MODE_STANDARD } mode = MODE_TIME;

      for (int i = 1; i + 1 < argc; i++) {
                   if (strcmp(argv[i], "--seconds") == 0) {
                       seconds = atoi(argv[i + 1]);
        } else if (strcmp(argv[i], "--mode") == 0) {
                       if (strcmp(argv[i + 1], "standard") == 0) mode = MODE_STANDARD;
                       else if (strcmp(argv[i + 1], "time") == 0) mode = MODE_TIME;
                   }
    }

    const char *map_path = NULL;
      if (argc >= 2 && argv[1][0] != '-') {
        map_path = argv[1];
    }

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

    if (listen(srv_fd, 8) < 0) {
        perror("listen");
        close(srv_fd);
        return 1;
    }

    printf("server: listening on %s\n", POS_SOCKET_PATH);

    int base_obstacles[GRID_H][GRID_W];
      memset(base_obstacles, 0, sizeof(base_obstacles));

    if (map_path) {
        server_ctx_t tmp;
        memset(&tmp, 0, sizeof(tmp));
        if (load_map_obstacles(base_obstacles, map_path) != 0) {
            fprintf(stderr, "server: failed to load map: %s\n", map_path);
            close(srv_fd);
            unlink(POS_SOCKET_PATH);
            return 1;
        }
        memcpy(base_obstacles, tmp.obstacles, sizeof(base_obstacles));
        printf("server: loaded map %s\n", map_path);
    } else {
        printf("server: no map (empty obstacles)\n");
    }

    while (1) {
        int cli_fd = accept(srv_fd, NULL, NULL);
        if (cli_fd < 0) {
            perror("accept");
            break;
        }

        char inbuf[128] = {0};
        ssize_t rn = read(cli_fd, inbuf, sizeof(inbuf) - 1);
        if (rn < 0) {
            perror("read");
            close(cli_fd);
            continue;
        }

        if (strcmp(inbuf, MSG_PING) == 0) {
            if (write_all(cli_fd, MSG_PONG, strlen(MSG_PONG)) < 0) {
                perror("write(PONG)");
                close(cli_fd);
                continue;
            }
        } else {
            fprintf(stderr, "server: expected PING\n");
            close(cli_fd);
            continue;
        }

        server_ctx_t ctx;
        memset(&ctx, 0, sizeof(ctx));
        ctx.cli_fd = cli_fd;
        ctx.running = 1;
        pthread_mutex_init(&ctx.lock, NULL);

        memcpy(ctx.obstacles, base_obstacles, sizeof(base_obstacles));
        
        ctx.start_time = time(NULL);
        if (mode == MODE_TIME) ctx.time_limit_sec = seconds;
        else ctx.time_limit_sec = 0;   

        ctx.len = 3;
        ctx.score = 0;

        int cx = GRID_W / 2;
        int cy = GRID_H / 2;
        ctx.sx[0] = cx;     ctx.sy[0] = cy;
        ctx.sx[1] = cx - 1; ctx.sy[1] = cy;
        ctx.sx[2] = cx - 2; ctx.sy[2] = cy;
        ctx.dir = DIR_RIGHT;

        spawn_fruit(&ctx);

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

        if (mode == MODE_TIME) {
            printf("server: time mode ended session -> exiting\n");
            break;
        }

        printf("server: standard mode ended session -> waiting 10s for rejoin...\n");

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(srv_fd, &rfds);

        struct timeval tv;
        tv.tv_sec = 10;
        tv.tv_usec = 0;

        int r = select(srv_fd + 1, &rfds, NULL, NULL, &tv);
        if (r < 0) {
            perror("select");
            break;
        }
        if (r == 0) {
            printf("server: nobody rejoined within 10s -> exiting\n");
            break;
        }

        printf("server: client available -> accepting...\n");
    }

    close(srv_fd);
    unlink(POS_SOCKET_PATH);
    return 0;
}

