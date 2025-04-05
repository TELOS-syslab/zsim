#!/bin/bash

# prepare redis
REDIS_HOME=/data/benchmarks/redis/redis-7.2.3/
YCSB_HOME=/data/benchmarks/ycsb/YCSB/

# $REDIS_HOME/src/redis-server $REDIS_HOME/redis.conf &

# $YCSB_HOME/bin/ycsb run redis -p redis.host=127.0.0.1 -p redis.port=6379 -P $YCSB_HOME/workloads/redis_ycsb_b -threads 1
# $YCSB_HOME/bin/ycsb run redis -p redis.host=127.0.0.1 -p redis.port=6379 -P $YCSB_HOME/workloads/redis_ycsb_d -threads 1
# $YCSB_HOME/bin/ycsb run redis -p redis.host=127.0.0.1 -p redis.port=6379 -P $YCSB_HOME/workloads/redis_ycsb_e -threads 1
# $YCSB_HOME/bin/ycsb run redis -p redis.host=127.0.0.1 -p redis.port=6379 -P $YCSB_HOME/workloads/workloada -threads 1

cd $YCSB_HOME
./bin/ycsb run redis -p redis.host=127.0.0.1 -p redis.port=6379 -P workloads/redis_ycsb_e -threads 1

cd -