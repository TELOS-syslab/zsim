#!/bin/bash
current_dir=$(dirname $0)
source $current_dir/env.sh

if [ -z "$ZSIMPATH" ]; then
    ZSIMPATH='.'
fi
cd $ZSIMPATH

FROM=108
TO=108
WINDOW_SIZE=1
STEP=1
USE_H5=true
TARGET="all" # hit, ipc, util, or all
VERBOSE=true
if [ "$USE_H5" == "true" ]; then
    WINDOW_SIZE=$((WINDOW_SIZE * 10))
    STEP=$((STEP * 10))
fi

# Function to process a single directory
process_stat() {
    local dir="$1"
    local use_h5="$2"
    local target="$3"
    local window_size="$4"
    local step="$5"
    local verbose="$6"
    echo "Processing directory: $dir"
    echo "use_f5: $use_h5, window_size: $window_size, step: $step"

    if [ "$use_h5" == "true" ]; then
        if [ "$verbose" == "true" ]; then
            if [ "$target" == "all" ]; then
                ./utils/parse/parse_stats_instr.py "$dir" -t hit -w "$window_size" -s "$step"  --plot -h5 -v
                ./utils/parse/parse_stats_instr.py "$dir" -t ipc -w "$window_size" -s "$step"  --plot -h5 -v
                ./utils/parse/parse_stats_instr.py "$dir" -t util -w "$window_size" -s "$step"  --plot -h5 -v
            else
                ./utils/parse/parse_stats_instr.py "$dir" -t $target -w "$window_size" -s "$step"  --plot -h5 -v
            fi
        else
            if [ "$target" == " " ]; then
                ./utils/parse/parse_stats_instr.py "$dir" -t hit -w "$window_size" -s "$step"  --plot -h5
                ./utils/parse/parse_stats_instr.py "$dir" -t ipc -w "$window_size" -s "$step"  --plot -h5
                ./utils/parse/parse_stats_instr.py "$dir" -t util -w "$window_size" -s "$step"  --plot -h5
            else
                ./utils/parse/parse_stats_instr.py "$dir" -t $target -w "$window_size" -s "$step"  --plot -h5
            fi
        fi
        
    else
        if [ "$verbose" == "true" ]; then
            if [ "$target" == "all  " ]; then
                ./utils/parse/parse_stats_instr.py "$dir" -t hit -w "$window_size" -s "$step"  --plot -v
                ./utils/parse/parse_stats_instr.py "$dir" -t ipc -w "$window_size" -s "$step"  --plot -v
                ./utils/parse/parse_stats_instr.py "$dir" -t util -w "$window_size" -s "$step"  --plot -v
            else
                ./utils/parse/parse_stats_instr.py "$dir" -t $target -w "$window_size" -s "$step"  --plot -v
            fi
        else
            if [ "$target" == "all" ]; then
                ./utils/parse/parse_stats_instr.py "$dir" -t hit -w "$window_size" -s "$step"  --plot
                ./utils/parse/parse_stats_instr.py "$dir" -t ipc -w "$window_size" -s "$step"  --plot
                ./utils/parse/parse_stats_instr.py "$dir" -t util -w "$window_size" -s "$step"  --plot
            else
                ./utils/parse/parse_stats_instr.py "$dir" -t $target -w "$window_size" -s "$step"  --plot
            fi
        fi
    fi
}

export -f process_stat  # Export the function for parallel execution

# Process directories in parallel
for pattern in `seq $FROM $TO`; do
    echo "Processing pattern: ${pattern}-*"
    # Find all matching directories and process them in parallel
    find output/results/${pattern}-*/*[\[\]]* -type d | \
        xargs -P 12 -I DIR bash -c "process_stat DIR \"$USE_H5\" \"$TARGET\" \"$WINDOW_SIZE\" \"$STEP\" \"$VERBOSE\""
    echo "========================================"

    # Combine results for this pattern - process each matching directory separately
    for dir in output/results/${pattern}-*; do
        if [ -d "$dir" ]; then
            ./utils/parse/parse_stats_instr.py "$dir" -t combine
        fi
    done
    echo "========================================"
done

cd -
