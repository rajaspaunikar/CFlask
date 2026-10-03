#!/usr/bin/env bash
#
# demo_outputs.sh - generate sample terminal output for Parts 1a, 1b, 1c
#
# Usage: ./scripts/demo_outputs.sh > demo_outputs.txt
# Run from the project root after `make`.

PORT=${PORT:-8080}
BIN=./bin/cflask
URL=http://127.0.0.1:$PORT
SLOW="$URL/arithmetic/prime?num=1000000000039"   # ~ms of CPU per request

show() {                       # print a command as typed, then its output
    echo "\$ $*"
    eval "${*/curl /curl -s }"   # -s: no progress meter when output is not a terminal
    echo
}

show_threads() {               # thread count of the running server
    echo "\$ ps -o nlwp= -p \$(pgrep -x cflask)"
    threads
    echo
}

start_server() {               # start cflask in the background, print its banner
    echo "--- Terminal 1 (server) ---"
    echo "\$ ./bin/cflask $*"
    $BIN "$@" > /tmp/cflask_demo.log 2>&1 &
    SPID=$!
    sleep 0.5
    cat /tmp/cflask_demo.log
    echo
    echo "--- Terminal 2 (client) ---"
}

stop_server() {
    kill "$SPID" 2>/dev/null
    wait "$SPID" 2>/dev/null
    sleep 0.3
}

threads() {                    # number of threads in the server process
    ps -o nlwp= -p "$SPID" | tr -d ' '
}

background_load() {            # 32 clients sending slow requests for a few seconds
    LOAD_PIDS=()
    for _ in $(seq 32); do
        ( for _ in $(seq 40); do curl -s -o /dev/null "$SLOW"; done ) &
        LOAD_PIDS+=($!)
    done
}

sample_threads() {             # print the thread count 5 times, 0.5 s apart
    echo "\$ for i in 1 2 3 4 5; do ps -o nlwp= -p \$(pgrep -x cflask); sleep 0.5; done"
    for _ in 1 2 3 4 5; do threads; sleep 0.5; done
    wait "${LOAD_PIDS[@]}" 2>/dev/null
    echo
}

[ -x "$BIN" ] || { echo "run 'make' first" >&2; exit 1; }

########################################################################
echo "================ Part 1a: single-threaded ================"
echo
echo "--- Missing arguments ---"
show "./bin/cflask"
start_server s "$PORT"
show "curl -i $URL/"
show "curl $URL/square"
show "curl $URL/cube"
show "curl $URL/helloworld"
show "curl $URL/pingpong"
show "curl -i $URL/nosuchpath"
echo "--- Threads in the server process ---"
show_threads
stop_server

########################################################################
echo "================ Part 1b: thread per request ================"
echo
start_server m "$PORT"
show "curl $URL/square"
show "curl $URL/helloworld"
echo "--- Threads while idle ---"
show_threads
echo "--- Threads under load (32 concurrent clients), sampled 5 times ---"
background_load
sleep 1
sample_threads
echo "--- Threads after the load ends ---"
show_threads
stop_server

########################################################################
echo "================ Part 1c: thread pool ================"
echo
start_server t "$PORT" 4
show "curl $URL/square"
show "curl $URL/helloworld"
echo "--- Threads while idle: main thread + 4 workers ---"
echo "\$ ps -T -p \$(pgrep -x cflask)"
ps -T -p "$SPID"
echo
echo "--- Threads under load (32 concurrent clients), sampled 5 times ---"
background_load
sleep 1
sample_threads
stop_server