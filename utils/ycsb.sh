#!/bin/bash

# prepare redis
REDIS_HOME=/data/benchmarks/redis/redis-7.2.3/
YCSB_HOME=/data/benchmarks/ycsb/YCSB/

# Get Redis port from netstat
get_redis_port() {
    netstat -atunp 2>/dev/null | grep "redis-serve" | awk '{print $4}' | cut -d':' -f2
}

PORT=$(get_redis_port)
if [ -z "$PORT" ]; then
    echo "Error: Redis port not found"
    exit 1
fi

echo "Using Redis port: $PORT"

set -x
cd $YCSB_HOME

$YCSB_HOME/bin/ycsb run redis -p redis.host=127.0.0.1 -p redis.port=$PORT -p redis.timeout=600000 -P $YCSB_HOME/workloads/redis_ycsb_b -threads 1
# $YCSB_HOME/bin/ycsb run redis -p redis.host=127.0.0.1 -p redis.port=$PORT -P $YCSB_HOME/workloads/redis_ycsb_d -threads 1
# $YCSB_HOME/bin/ycsb run redis -p redis.host=127.0.0.1 -p redis.port=$PORT -P $YCSB_HOME/workloads/redis_ycsb_e -threads 1
# $YCSB_HOME/bin/ycsb run redis -p redis.host=127.0.0.1 -p redis.port=$PORT -P $YCSB_HOME/workloads/workloada -threads 1

cd -