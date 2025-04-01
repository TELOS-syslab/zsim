#!/bin/bash
current_dir=$(dirname $0)
source $current_dir/env.sh

if [ -z "$ZSIMPATH" ]; then
    ZSIMPATH='.'
fi


./utils/parse/parse_stats.py output/server03/20250401-003025[idealfully-cc]/zsim-pout.out root.mem.mem-0.idealFullyCache.loadHit 20 plot
