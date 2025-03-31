#!/bin/bash
current_dir=$(dirname $0)
source $current_dir/env.sh

if [ -z "$ZSIMPATH" ]; then
    ZSIMPATH='.'
fi



cache_scheme=$1
category=$2

#echo 18446744073692774399 > /proc/sys/kernel/shmmax
sudo sysctl -w kernel.shmmax=18446744073692774399
sudo sysctl -w kernel.yama.ptrace_scope=0

cd $ZSIMPATH

# ./bin/zsim tests/debug.cfg output
# ./bin/zsim tests/debug-dramsim2.cfg output
# ./bin/zsim tests/debug-dramsim3.cfg output
# ./bin/zsim tests/debug-ddr-dramsim3.cfg output
# ./bin/zsim tests/simple_dramsim2.cfg output
#  ./bin/zsim tests/simple_dramsim3.cfg output

# ./bin/zsim tests/test-alloy.cfg output alloy-pr
# ./bin/zsim tests/test-ndc-8192.cfg output ndc
# ./bin/zsim tests/test-ndc-4096.cfg output ndc-pr
# ./bin/zsim tests/test-ndc-2048.cfg output ndc
# ./bin/zsim tests/test-nocache.cfg output nocache
# ./bin/zsim tests/test-cacheonly.cfg output cacheonly
# ./bin/zsim tests/test-debug.cfg output debug
# ./bin/zsim tests/test-idealbalanced.cfg output idealbalanced-pr
if [ $category == "benchmarks" ]; then
    benchmark="default"
    if [ $# -ge 3 ]; then
        benchmark=$3
    fi
    ./bin/zsim tests/$category/$benchmark/test-$cache_scheme.cfg output $cache_scheme-$benchmark
else
    ./bin/zsim tests/$category/test-$cache_scheme.cfg output $cache_scheme
fi
cd -
