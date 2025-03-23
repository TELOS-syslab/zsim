#!/bin/bash
current_dir=$(dirname $0)
source $current_dir/env.sh

if [ -z "$ZSIMPATH" ]; then
    ZSIMPATH='.'
fi

#echo 18446744073692774399 > /proc/sys/kernel/shmmax
sudo sysctl -w kernel.shmmax=18446744073692774399
sudo sysctl -w kernel.yama.ptrace_scope=0

cd $ZSIMPATH
# ./bin/zsim tests/debug.cfg output
./bin/zsim tests/debug-dsim.cfg output
# ./bin/zsim tests/simple_dramsim2.cfg output
# ./bin/zsim tests/simple_dramsim3.cfg output
cd -
