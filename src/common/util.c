#include "common/util.h"
#include <unistd.h>

int write_all(int fd, const char *buf, size_t len) {
      size_t off = 0;
      while (off < len) {
        ssize_t n = write(fd, buf + off, len - off);
        if (n < 0) return -1;
        off += (size_t)n;
    }
    return 0;
}

