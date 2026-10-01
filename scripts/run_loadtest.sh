#!/usr/bin/env bash
#
# run_loadtest.sh - sweep ab concurrency for several server configurations
#
# Usage:  ./scripts/run_loadtest.sh <name> "<url path + query>"
# e.g.    ./scripts/run_loadtest.sh loadtest1 "/arithmetic/prime?num=1000000007"
#
# Output: plots/<name>.data        summary table (+ commands used)
#         plots/raw/<name>/*.txt   full ab output of every run
#
# Tunables (override via environment):
#   PORT, N, SERVER_CORES, CLIENT_CORES, THREADS_LIST, CONC_LIST
#   e.g. N=50000 CONC_LIST="1 4 16 64" ./scripts/run_loadtest.sh ...

set -euo pipefail

NAME=${1:?usage: $0 <name> <url-path>}
URLPATH=${2:?usage: $0 <name> <url-path>}

PORT=${PORT:-8080}
N=${N:-100000}
SERVER_CORES=${SERVER_CORES:-0-1}     # cflask is pinned here
CLIENT_CORES=${CLIENT_CORES:-4}       # ab is single-threaded: one core is enough
THREADS_LIST=${THREADS_LIST:-"single 1 2 4 8"}
CONC_LIST=${CONC_LIST:-"1 2 4 8 16 32 64 128"}

URL="http://127.0.0.1:${PORT}${URLPATH}"
OUT=plots
RAW=$OUT/raw/$NAME
DATA=$OUT/$NAME.data
mkdir -p "$RAW"

command -v ab >/dev/null || { echo "ab not found: sudo apt install apache2-utils"; exit 1; }
[ -x bin/cflask ] && [ -x bin/cflask_single ] || { echo "run 'make' first"; exit 1; }

{
    echo "# test:          $NAME"
    echo "# url:           $URL"
    echo "# date:          $(date)"
    echo "# host:          $(uname -sr), $(nproc) cpus, $(lscpu | awk -F: '/Model name/ {gsub(/^ +/,"",$2); print $2; exit}')"
    echo "# server cmd:    taskset -c $SERVER_CORES ./bin/cflask $PORT <threads>   (or ./bin/cflask_single $PORT)"
    echo "# client cmd:    taskset -c $CLIENT_CORES ab -r -n $N -c <concurrency> \"$URL\""
    echo "# threads:       single = cflask_single (no pool), k = cflask with k workers"
    echo "#"
    echo "# threads concurrency throughput_rps mean_latency_ms failed"
} > "$DATA"

for T in $THREADS_LIST; do
    if [ "$T" = "single" ]; then
        SERVER="./bin/cflask_single $PORT"
    else
        SERVER="./bin/cflask $PORT $T"
    fi

    echo ">>> [$NAME] server: $SERVER (cores $SERVER_CORES)"
    taskset -c "$SERVER_CORES" $SERVER > /dev/null 2>&1 &
    SPID=$!

    # wait until the server accepts connections
    for _ in $(seq 50); do
        curl -s -o /dev/null "http://127.0.0.1:$PORT/" && break
        sleep 0.1
    done

    # warm-up run (discarded)
    taskset -c "$CLIENT_CORES" ab -q -r -n 2000 -c 8 "$URL" > /dev/null 2>&1 || true

    for C in $CONC_LIST; do
        F=$RAW/t${T}_c${C}.txt
        CMD="taskset -c $CLIENT_CORES ab -r -n $N -c $C $URL"
        echo "# command: $CMD" > "$F"
        $CMD >> "$F" 2>&1 || true

        RPS=$(awk '/^Requests per second/ {print $4}' "$F")
        LAT=$(awk '/^Time per request/ && /\(mean\)$/ {print $4; exit}' "$F")
        FAIL=$(awk '/^Failed requests/ {print $3}' "$F")
        printf "%-7s %-5s %-10s %-10s %s\n" "$T" "$C" "${RPS:-NA}" "${LAT:-NA}" "${FAIL:-NA}" | tee -a "$DATA"
    done

    kill "$SPID" 2>/dev/null || true
    wait "$SPID" 2>/dev/null || true
    sleep 1
done

echo "done -> $DATA"