#!/bin/bash

# prepare redis
REDIS_HOME=/data/benchmarks/redis/redis-7.2.3/
YCSB_HOME=/data/benchmarks/ycsb/YCSB/

# Check if required directories exist
for dir in "$REDIS_HOME" "$YCSB_HOME"; do
    if [ ! -d "$dir" ]; then
        echo "Error: Directory $dir does not exist"
        exit 1
    fi
done

# Get Redis port from netstat
get_redis_ports() {
    netstat -atunp 2>/dev/null | grep "redis-serve" | awk '{print $4}' | cut -d':' -f2
}

# Initialize ports array
declare -a ports

if [ -n "$1" ]; then
    ports=("$1")
else
    readarray -t ports < <(get_redis_ports)
    if [ ${#ports[@]} -eq 0 ]; then
        echo "Error: Redis port not found"
        exit 1
    fi
fi

echo "Using Redis ports: ${ports[*]}"

set -x
cd "$YCSB_HOME" || exit 1

for port in "${ports[@]}"; do
    echo "Running YCSB on Redis port: $port"
    "$YCSB_HOME/bin/ycsb" run redis \
        -p "redis.host=127.0.0.1" \
        -p "redis.port=$port" \
        -p "redis.timeout=600000" \
        -p "redis.pipeline.sync.ops=1000000" \
        -p "redis.pipeline.sync.time=10000000" \
        -P "$YCSB_HOME/workloads/redis_ycsb_d" \
        -threads 1 &
done

cd - || exit 1