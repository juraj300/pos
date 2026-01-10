#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "common/ipc.h"

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

    if (write(fd, MSG_PING, strlen(MSG_PING)) < 0) { perror("write"); close(fd); return 1; }
    printf("client: sent: %s", MSG_PING);

    char buf[128] = {0};
      ssize_t n = read(fd, buf, sizeof(buf) - 1);
      if (n < 0) { perror("read"); close(fd); return 1; }

    printf("client: received: %s", buf);

    close(fd);
    return 0;
}
