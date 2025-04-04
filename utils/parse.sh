#!/bin/bash
current_dir=$(dirname $0)
source $current_dir/env.sh

if [ -z "$ZSIMPATH" ]; then
    ZSIMPATH='.'
fi
cd $ZSIMPATH

FROM=38
TO=100
WINDOW_SIZE=10
STEP=10
USE_H5=true
VERBOSE=true
if [ "$USE_H5" == "true" ]; then
    WINDOW_SIZE=$((WINDOW_SIZE * 10))
    STEP=$((STEP * 10))
fi

# Function to process a single directory
process_stat() {
    local dir="$1"
    local use_h5="$2"
    local window_size="$3"
    local step="$4"
    local verbose="$5"
    echo "Processing directory: $dir"
    echo "use_f5: $use_h5, window_size: $window_size, step: $step"

    if [ "$use_h5" == "true" ]; then
        if [ "$verbose" == "true" ]; then
            ./utils/parse/parse_stats.py "$dir" -t hit -w "$window_size" -s "$step"  --plot -h5 -v
            # ./utils/parse/parse_stats.py "$dir" -t ipc -w "$window_size" -s "$step"  --plot -h5 -v
        else
            ./utils/parse/parse_stats.py "$dir" -t hit -w "$window_size" -s "$step"  --plot -h5
            # ./utils/parse/parse_stats.py "$dir" -t ipc -w "$window_size" -s "$step"  --plot -h5
        fi
        
    else
        if [ "$verbose" == "true" ]; then
            ./utils/parse/parse_stats.py "$dir" -t hit -w "$window_size" -s "$step"  --plot -v
            ./utils/parse/parse_stats.py "$dir" -t ipc -w "$window_size" -s "$step"  --plot -v
        else
            ./utils/parse/parse_stats.py "$dir" -t hit -w "$window_size" -s "$step"  --plot
            ./utils/parse/parse_stats.py "$dir" -t ipc -w "$window_size" -s "$step"  --plot
        fi
    fi
}

export -f process_stat  # Export the function for parallel execution

# Process directories in parallel
for pattern in `seq $FROM $TO`; do
    echo "Processing pattern: ${pattern}*"
    # Find all matching directories and process them in parallel
    find output/collect/${pattern}*/*[\[\]]* -type d | \
        xargs -P 16 -I DIR bash -c "process_stat DIR \"$USE_H5\" \"$WINDOW_SIZE\" \"$STEP\" \"$VERBOSE\""
    echo "========================================"

    # Combine results for this pattern - process each matching directory separately
    for dir in output/collect/${pattern}*; do
        if [ -d "$dir" ]; then
            ./utils/parse/parse_stats.py "$dir" -t combine
        fi
    done
    echo "========================================"
done

cd -
