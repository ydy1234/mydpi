CC = gcc
CFLAGS = -Wextra -Wall -g -O0 -DDEBUG -lpthread -l sqlite3

SOURCES = main.c
BIN_NAME = mynetlink

all: $(BIN_NAME)

$(BIN_NAME): $(SOURCES)
	$(CC)  $^ -o $@ $(CFLAGS)
	cp $(BIN_NAME) ../$(BIN_NAME)
clean:
	rm -rf $(BIN_NAME) ../$(BIN_NAME)
