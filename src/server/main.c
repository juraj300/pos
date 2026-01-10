#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "common/ipc.h"

int main(void) {
      int srv_fd = socket(AF_UNIX, SOCK_STREAM, 0);
      if (srv_fd < 0) { perror("socket"); return 1; }

    struct sockaddr_un addr;
      memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, POS_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    unlink(POS_SOCKET_PATH); 

    if (bind(srv_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(srv_fd); return 1;
    }

    if (listen(srv_fd, 1) < 0) {
        perror("listen"); close(srv_fd); return 1;
    }

    printf("server: listening on %s\n", POS_SOCKET_PATH);

    int cli_fd = accept(srv_fd, NULL, NULL);
      if (cli_fd < 0) { perror("accept"); close(srv_fd); return 1; }

    char buf[128] = {0};
      ssize_t n = read(cli_fd, buf, sizeof(buf) - 1);
      if (n < 0) { perror("read"); close(cli_fd); close(srv_fd); return 1; }

    printf("server: received: %s", buf);

    if (strcmp(buf, MSG_PING) == 0) {
        if (write(cli_fd, MSG_PONG, strlen(MSG_PONG)) < 0) perror("write");
        printf("server: sent: %s", MSG_PONG);
    } else {
        printf("server: unknown message\n");
    }

    close(cli_fd);
    close(srv_fd);
    unlink(POS_SOCKET_PATH);
    return 0;
}

