#!/bin/bash

# prepare redis
REDIS_HOME=/data/benchmarks/redis/redis-7.2.3/
YCSB_HOME=/data/benchmarks/ycsb/YCSB/

cmd=""
if [ -n "$1" ]; then
    cmd="$1"
fi
echo "Command: $cmd"

# Check if required directories exist
for dir in "$REDIS_HOME" "$YCSB_HOME"; do
    if [ ! -d "$dir" ]; then
        echo "Error: Directory $dir does not exist"
        exit 1
    fi
done

# Get Redis port from netstat
function get_redis_ports() {
    netstat -atunp 2>/dev/null | grep "redis-serve" | awk '{print $4}' | cut -d':' -f2
}

function kill_ycsb() {
    echo "Finding YCSB processes..."
    ycsb_pids=$(ps aux | grep "site.ycsb.Client" | grep -v grep | awk '{print $2}')
    if [ -n "$ycsb_pids" ]; then
        echo "Found YCSB processes with PIDs: $ycsb_pids"
        echo "Killing YCSB processes..."
        for pid in $ycsb_pids; do
            kill -9 "$pid" 2>/dev/null
            echo "Killed PID: $pid"
        done
        echo "All YCSB processes terminated"
    else
        echo "No YCSB processes found"
    fi
}

function run_ycsb() {
    # Create logs directory
    LOG_DIR="/tmp/ycsb_logs"
    if [ -d "$LOG_DIR" ]; then
        rm -rf "$LOG_DIR"
    fi
    mkdir -p "$LOG_DIR"

    # Initialize ports array
    declare -a ports
    readarray -t ports < <(get_redis_ports)
    if [ ${#ports[@]} -eq 0 ]; then
        echo "Error: Redis port not found"
        exit 1
    fi

    echo "Using Redis ports: ${ports[*]}"


    cd "$YCSB_HOME" || exit 1
    for port in "${ports[@]}"; do
        echo "Running YCSB on Redis port: $port"
        LOG_FILE="$LOG_DIR/ycsb_redis_${port}_$(date +%Y%m%d_%H%M%S).log"
        "$YCSB_HOME/bin/ycsb" run redis \
            -p "redis.host=127.0.0.1" \
            -p "redis.port=$port" \
            -p "redis.timeout=600000" \
            -p "redis.pipeline.sync.ops=100000000" \
            -p "redis.pipeline.sync.time=1000000000" \
            -P "$YCSB_HOME/workloads/redis_ycsb_e" \
            -threads 1 > "$LOG_FILE" 2>&1 &
        echo "Started YCSB for port $port. Logging to $LOG_FILE"
    
        echo -n "Waiting 10 seconds before next task"
        for ((i=0; i<10; i++)); do
            echo -n "."
            sleep 1
        done
        echo ""
    done
    echo "All YCSB tasks started. Check logs in $LOG_DIR"
    cd - || exit 1
}

if [ "$cmd" == "kill" ]; then
    kill_ycsb
    exit 0
elif [ "$cmd" == "run" ]; then
    run_ycsb
else
    echo "Usage: $0 [kill|run]"
    exit 1
fi


