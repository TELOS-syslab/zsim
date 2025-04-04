#!/bin/bash
current_dir=$(dirname $0)
source $current_dir/env.sh

if [ -z "$ZSIMPATH" ]; then
    ZSIMPATH='.'
fi

cd $ZSIMPATH

output_dir="$ZSIMPATH/output/server03/12-cc-web-johnny"
# output_dir="$ZSIMPATH/output/server03/4"
for dir in "$output_dir"/*[\[\]]*; do
    if [ -d "$dir" ]; then
        echo "Processing directory: $dir"
        # Process both hit rates and IPC
        ./utils/parse/parse_stats.py "$dir" hit 100 100 plot
        ./utils/parse/parse_stats.py "$dir" ipc 100 1 plot
        # exit 0
        echo "----------------------------------------"
    fi
done

# ./utils/parse/parse_stats.py output/server03/20250331-023121[alloy-pr] hit 100 plot
./utils/parse/parse_stats.py $output_dir combine


output_dir="$ZSIMPATH/output/server03/13-cc-web-random"
# output_dir="$ZSIMPATH/output/server03/4"
for dir in "$output_dir"/*[\[\]]*; do
    if [ -d "$dir" ]; then
        echo "Processing directory: $dir"
        # Process both hit rates and IPC
        ./utils/parse/parse_stats.py "$dir" hit 100 100 plot
        ./utils/parse/parse_stats.py "$dir" ipc 100 1 plot
        # exit 0
        echo "----------------------------------------"
    fi
done

# ./utils/parse/parse_stats.py output/server03/20250331-023121[alloy-pr] hit 100 plot
./utils/parse/parse_stats.py $output_dir combine


cd -