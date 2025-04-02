#!/bin/bash
current_dir=$(dirname $0)
source $current_dir/env.sh

if [ -z "$ZSIMPATH" ]; then
    ZSIMPATH='.'
fi



benchmark=""
category=$1
if [[ $category == "debug" ]]; then
    cache_scheme=$2
else
    benchmark=$2
    cache_scheme=$3
fi


#echo 18446744073692774399 > /proc/sys/kernel/shmmax
# sudo sysctl -w kernel.shmmax=18446744073692774399
# sudo sysctl -w kernel.yama.ptrace_scope=0

cd $ZSIMPATH
if [ ! -d "output" ]; then
    mkdir output
fi
if [ ! -d "tests/$category" ]; then
    echo "No such test $category"
    exit 1
fi

if [ $benchmark != "" ]; then
    if [ ! -d "tests/$category/$benchmark" ]; then
        echo "No such test $category/$benchmark"
        exit 1
    fi
    if [ ! -f "tests/$category/$benchmark/test-$cache_scheme.cfg" ]; then
        echo "No such test $category/$benchmark/test-$cache_scheme.cfg"
        exit 1
    fi
    ./bin/zsim tests/$category/$benchmark/test-$cache_scheme.cfg output $cache_scheme"_"$benchmark"_"$category
else
    if [ ! -f "tests/$category/test-$cache_scheme.cfg" ]; then
        echo "No such test $category/test-$cache_scheme.cfg"
        exit 1
    fi
    ./bin/zsim tests/$category/test-$cache_scheme.cfg output $cache_scheme"_"$category
fi

cd -
