CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -O2 -Iinclude -pthread

CLIENT_SRCS := src/client/main.c
SERVER_SRCS := src/server/main.c
COMMON_SRCS := src/common/util.c src/common/map.c


.PHONY: all client server clean

all: client server

client: $(CLIENT_SRCS) $(COMMON_SRCS)
		$(CC) $(CFLAGS) -o client $(CLIENT_SRCS) $(COMMON_SRCS)

server: $(SERVER_SRCS) $(COMMON_SRCS)
		$(CC) $(CFLAGS) -o server $(SERVER_SRCS) $(COMMON_SRCS)

clean:
	rm -f client server
