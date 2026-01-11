#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "common/ipc.h"
#include "common/util.h"
#include "common/map.h"


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
    int paused;
    int freeze_left;
} client_ctx_t;

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
    printf("score=%d tick=%d time_left=%d paused=%d freeze_left=%d\n",
                ctx->score, ctx->tick, ctx->time_left, ctx->paused, ctx->freeze_left);
    printf("(w/a/s/d + Enter) p=pause/resume, q=quit\n");

    fflush(stdout);
}

static void handle_line(client_ctx_t *ctx, const char *line) {
      if (strncmp(line, "GAMEOVER ", 9) == 0) {
          printf("\n=== %s ===\n", line);
          ctx->running = 0;
          return;
      }

      int len, score, tick, time_left, paused, freeze_left;
      int off = 0;

      if (sscanf(line, "STATE %d %d %d %d %d %d %n", &len, &score, &tick, &time_left, &paused, &freeze_left, &off) == 6) {

        pthread_mutex_lock(&ctx->lock);
        ctx->len = len;
        ctx->score = score;
        ctx->tick = tick;
        ctx->time_left = time_left;
        ctx->paused = paused;
        ctx->freeze_left = freeze_left;


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

static int start_server_process(const char *map_path, const char *mode, int seconds) {
      pid_t pid = fork();
      if (pid < 0) {
        perror("fork");
        return -1;
    }
      if (pid == 0) {
        char secbuf[16];
        snprintf(secbuf, sizeof(secbuf), "%d", seconds);

        if (map_path && strcmp(mode, "time") == 0 && seconds > 0) {
            execl("./server", "./server", map_path, "--mode", "time", "--seconds", secbuf, (char*)NULL);
        } else if (map_path && strcmp(mode, "standard") == 0) {
            execl("./server", "./server", map_path, "--mode", "standard", (char*)NULL);
        } else if (!map_path && strcmp(mode, "time") == 0 && seconds > 0) {
            execl("./server", "./server", "--mode", "time", "--seconds", secbuf, (char*)NULL);
        } else {
            execl("./server", "./server", "--mode", "standard", (char*)NULL);
        }

        perror("execl");
        _exit(127);
    }
      return 0;
}

int main(int argc, char **argv) {
      (void)argc; (void)argv;

    const char *map_path = "maps/map1.txt";

      while (1) {
        printf("\n=== POS Snake ===\n");
        printf("1) Nova hra (spusti server)\n");
        printf("2) Pripojit sa k hre\n");
        printf("3) Koniec\n");
        printf("> ");
        fflush(stdout);

        char choice[16];
        if (!fgets(choice, sizeof(choice), stdin)) return 0;

        if (choice[0] == '3') return 0;


        printf("Mapa (1/2/3) [1]: ");
        fflush(stdout);
        char mline[16];
        if (fgets(mline, sizeof(mline), stdin)) {
            if (mline[0] == '2') map_path = "maps/map2.txt";
            else if (mline[0] == '3') map_path = "maps/map3.txt";
            else map_path = "maps/map1.txt";
        }

        if (choice[0] == '1') {

            char mode[16] = "standard";
            int seconds = 30;

            printf("Rezim (s=standard, t=time) [s]: ");
            fflush(stdout);
            char rline[16];
            if (fgets(rline, sizeof(rline), stdin) && (rline[0] == 't' || rline[0] == 'T')) {
                strcpy(mode, "time");
                printf("Seconds [30]: ");
                fflush(stdout);
                char sline[32];
                if (fgets(sline, sizeof(sline), stdin)) {
                    int tmp = atoi(sline);
                    if (tmp > 0) seconds = tmp;
                }
            } else {
                strcpy(mode, "standard");
            }

            if (start_server_process(map_path, mode, seconds) != 0) {
                printf("Nepodarilo sa spustit server.\n");
                continue;
            }


            usleep(200000);
        } else if (choice[0] != '2') {
            printf("Zla volba.\n");
            continue;
        }

        break;
    }

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
    if (load_map_obstacles(ctx.obstacles, map_path) != 0) {
        fprintf(stderr, "client: failed to load map: %s\n", map_path);
        close(fd);
        return 1;
    }

    ctx.sx[0] = 0; ctx.sy[0] = 0;
    ctx.sx[1] = 0; ctx.sy[1] = 0;
    ctx.sx[2] = 0; ctx.sy[2] = 0;

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

 
