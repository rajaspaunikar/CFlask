CC     = gcc
CFLAGS = -O2 -Wall -Wextra -pthread -I.
COMMON = http-parser.c fn/fn.c
HDRS   = http-parser.h fn/fn.h fn/fnlist.h
BIN    = bin

# make LOG=1  -> log the serving thread of every request (not for load tests)
ifdef LOG
CFLAGS += -DLOG_REQUESTS
endif

all: $(BIN)/cflask

$(BIN):
	mkdir -p $(BIN)

# ./bin/cflask s <port> | m <port> <max_threads> | t <port> <num_threads>
$(BIN)/cflask: cflask/cflask.c $(COMMON) $(HDRS) | $(BIN)
	$(CC) $(CFLAGS) -o $@ cflask/cflask.c $(COMMON)


clean:
	rm -rf $(BIN)

.PHONY: all clean