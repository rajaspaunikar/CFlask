CC     = gcc
CFLAGS = -O2 -Wall -Wextra -pthread -I.
COMMON = http-parser.c fn/fn.c
HDRS   = http-parser.h fn/fn.h fn/fnlist.h
BIN    = bin

all: $(BIN)/cflask $(BIN)/cflask_single $(BIN)/parser_demo

$(BIN):
	mkdir -p $(BIN)

# Step 4: thread-pool server      -> ./bin/cflask <port> <num_threads>
$(BIN)/cflask: cflask/cflask_multi.c $(COMMON) $(HDRS) | $(BIN)
	$(CC) $(CFLAGS) -o $@ cflask/cflask_multi.c $(COMMON)

# Step 3: single-threaded server  -> ./bin/cflask_single <port>
$(BIN)/cflask_single: cflask/cflask_single.c $(COMMON) $(HDRS) | $(BIN)
	$(CC) $(CFLAGS) -o $@ cflask/cflask_single.c $(COMMON)

# Part 0.1: parser demo           -> ./bin/parser_demo
$(BIN)/parser_demo: parser_demo.c http-parser.c http-parser.h | $(BIN)
	$(CC) $(CFLAGS) -o $@ parser_demo.c http-parser.c

clean:
	rm -rf $(BIN)

.PHONY: all clean