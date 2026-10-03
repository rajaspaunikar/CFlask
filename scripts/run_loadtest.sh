#!/usr/bin/env bash
#
# run_loadtest.sh - sweep concurrency for several server configurations
#
# Usage:  ./scripts/run_loadtest.sh <name> "<url path + query>"
# e.g.    ./scripts/run_loadtest.sh loadtest1 "/work?cpu=1000&io=0"
#
# Output: plots/<name>.data        summary table (+ commands used)
#         plots/raw/<name>/*.txt   full load-generator output of every run
#
# Tunables (override via environment):
#   LOADGEN       wrk (default) or ab
#   PORT          server port                         (default 8080)
#   SERVER_CORES  taskset list for cflask             (default 0,1)
#   CLIENT_CORES  taskset list for the load generator (default 4,5)
#   CONFIGS       s = single-threaded, m = thread per request, tN = pool of N
#                                                     (default "s m t1 t2 t4 t8")
#   CONC_LIST     concurrency values                  (default "1 2 4 8 16 32 64 128")
#   DURATION      wrk only: length of each run        (default 30s)
#   WRK_THREADS   wrk only: max client threads        (default 2)
#   N             ab only: requests per run           (default 100000)

set -euo pipefail

NAME=${1:?usage: $0 <name> <url-path>}
URLPATH=${2:?usage: $0 <name> <url-path>}

LOADGEN=${LOADGEN:-wrk}
PORT=${PORT:-8080}
SERVER_CORES=${SERVER_CORES:-0,1}
CLIENT_CORES=${CLIENT_CORES:-4,5}
CONFIGS=${CONFIGS:-"s m t1 t2 t4 t8"}
CONC_LIST=${CONC_LIST:-"1 2 4 8 16 32 64 128"}
DURATION=${DURATION:-30s}
WRK_THREADS=${WRK_THREADS:-2}
N=${N:-100000}

URL="http://127.0.0.1:${PORT}${URLPATH}"
OUT=plots
RAW=$OUT/raw/$NAME
DATA=$OUT/$NAME.data
mkdir -p "$RAW"

case "$LOADGEN" in
    wrk) CLIENT_DESC="taskset -c $CLIENT_CORES wrk -t<min(c,$WRK_THREADS)> -c<c> -d$DURATION -H \"Connection: close\" \"$URL\"" ;;
    ab)  CLIENT_DESC="taskset -c $CLIENT_CORES ab -r -n $N -c<c> \"$URL\"" ;;
    *)   echo "LOADGEN must be wrk or ab"; exit 1 ;;
esac
command -v "$LOADGEN" >/dev/null || { echo "$LOADGEN not found"; exit 1; }
[ -x bin/cflask ] || { echo "run 'make' first"; exit 1; }

{
    echo "# test:          $NAME"
    echo "# url:           $URL"
    echo "# date:          $(date)"
    echo "# host:          $(uname -sr), $(nproc) cpus, $(lscpu | awk -F: '/Model name/ {gsub(/^ +/,"",$2); print $2; exit}')"
    echo "# server cmd:    taskset -c $SERVER_CORES ./bin/cflask <mode> $PORT [threads]"
    echo "# client cmd:    $CLIENT_DESC"
    echo "# loadgen:       $LOADGEN"
    echo "# config:        s = single-threaded, m = thread per request, tN = thread pool of N"
    echo "#"
    echo "# config concurrency throughput_rps mean_latency_ms errors"
} > "$DATA"

# convert a wrk latency value (e.g. 535.23us, 1.37ms, 1.02s) to milliseconds
to_ms() {
    awk -v v="$1" 'BEGIN {
        if (v ~ /us$/)      { sub(/us$/, "", v); printf "%.4f", v / 1000 }
        else if (v ~ /ms$/) { sub(/ms$/, "", v); printf "%.4f", v }
        else if (v ~ /m$/)  { sub(/m$/,  "", v); printf "%.4f", v * 60000 }
        else if (v ~ /s$/)  { sub(/s$/,  "", v); printf "%.4f", v * 1000 }
        else                { print "NA" }
    }'
}

run_client() {          # $1 = concurrency, $2 = output file; prints "rps lat_ms errors"
    local C=$1 F=$2
    if [ "$LOADGEN" = wrk ]; then
        local T=$(( C < WRK_THREADS ? C : WRK_THREADS ))
        local CMD="taskset -c $CLIENT_CORES wrk -t$T -c$C -d$DURATION -H \"Connection: close\" \"$URL\""
        echo "# command: $CMD" > "$F"
        taskset -c "$CLIENT_CORES" wrk -t"$T" -c"$C" -d"$DURATION" -H "Connection: close" "$URL" >> "$F" 2>&1 || true
        local RPS LAT ERR
        RPS=$(awk '/^Requests\/sec/ {print $2}' "$F")
        LAT=$(to_ms "$(awk '/^ +Latency/ {print $2; exit}' "$F")")
        ERR=$(awk '/Socket errors/ {gsub(/,/,""); e += $4 + $6 + $8 + $10}
                   /Non-2xx/ {e += $NF} END {print e + 0}' "$F")
        echo "${RPS:-NA} ${LAT:-NA} ${ERR:-NA}"
    else
        local CMD="taskset -c $CLIENT_CORES ab -r -n $N -c $C $URL"
        echo "# command: $CMD" > "$F"
        $CMD >> "$F" 2>&1 || true
        local RPS LAT ERR
        RPS=$(awk '/^Requests per second/ {print $4}' "$F")
        LAT=$(awk '/^Time per request/ && /\(mean\)$/ {print $4; exit}' "$F")
        ERR=$(awk '/^Failed requests/ {print $3}' "$F")
        echo "${RPS:-NA} ${LAT:-NA} ${ERR:-NA}"
    fi
}

for CFG in $CONFIGS; do
    MODE=${CFG:0:1}
    NUM=${CFG:1}
    case "$MODE" in
        s|m) SERVER="./bin/cflask $MODE $PORT" ;;
        t)   [ -n "$NUM" ] || { echo "config '$CFG' needs a thread count, e.g. t4"; exit 1; }
             SERVER="./bin/cflask t $PORT $NUM" ;;
        *)   echo "unknown config '$CFG' (use s, m or tN)"; exit 1 ;;
    esac

    echo ">>> [$NAME] server: $SERVER (cores $SERVER_CORES), client: $LOADGEN (cores $CLIENT_CORES)"
    taskset -c "$SERVER_CORES" $SERVER > /dev/null 2>&1 &
    SPID=$!
    for _ in $(seq 50); do
        curl -s -o /dev/null "http://127.0.0.1:$PORT/" && break
        sleep 0.1
    done

    # warm-up (discarded)
    if [ "$LOADGEN" = wrk ]; then
        taskset -c "$CLIENT_CORES" wrk -t1 -c8 -d3s -H "Connection: close" "$URL" > /dev/null 2>&1 || true
    else
        taskset -c "$CLIENT_CORES" ab -q -r -n 2000 -c 8 "$URL" > /dev/null 2>&1 || true
    fi

    for C in $CONC_LIST; do
        read -r RPS LAT ERR < <(run_client "$C" "$RAW/${CFG}_c${C}.txt")
        printf "%-7s %-5s %-10s %-10s %s\n" "$CFG" "$C" "$RPS" "$LAT" "$ERR" | tee -a "$DATA"
    done

    kill "$SPID" 2>/dev/null || true
    wait "$SPID" 2>/dev/null || true
    sleep 1
done

echo "done -> $DATA"