CC := gcc
CFLAGS := -Wall -Wextra -g

all: server client

server: server.o markdown.o
	$(CC) $(CFLAGS) -o server server.o markdown.o

client: client.o markdown.o
	$(CC) $(CFLAGS) -o client client.o markdown.o

server.o: source/server.c libs/markdown.h libs/document.h
	$(CC) $(CFLAGS) -c source/server.c -o server.o

client.o: source/client.c libs/markdown.h libs/document.h
	$(CC) $(CFLAGS) -c source/client.c -o client.o

markdown.o: source/markdown.c libs/markdown.h libs/document.h
	$(CC) $(CFLAGS) -c source/markdown.c -o markdown.o

.PHONY: clean
clean:
	rm -f server client *.o
