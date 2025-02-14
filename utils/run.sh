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
./bin/zsim tests/debug.cfg output
cd -
