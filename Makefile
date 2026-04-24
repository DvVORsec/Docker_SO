CC = gcc
CFLAGS = -Wall -pthread

all: server server_unsafe client

server: src/server.c
	$(CC) $(CFLAGS) -o server src/server.c

server_unsafe: src/server_unsafe.c
	$(CC) $(CFLAGS) -o server_unsafe src/server_unsafe.c

client: src/client.c
	$(CC) $(CFLAGS) -o client src/client.c

clean:
	rm -f server server_unsafe client
