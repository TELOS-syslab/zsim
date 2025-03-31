#!/usr/bin/env python3

import re
import sys
import math
import os
import time
from typing import List, Dict, Any, Union, Tuple, Optional
from functools import lru_cache

# Add imports for plotting and efficient numerical operations
try:
    import numpy as np
    import matplotlib.pyplot as plt
    import seaborn as sns
    PLOTTING_AVAILABLE = True
except ImportError:
    PLOTTING_AVAILABLE = False

# Cache to avoid re-parsing the same file multiple times
_PARSE_CACHE = {}


@lru_cache(maxsize=8)
def parse_zsim_output(file_path: str) -> List[Dict[str, Any]]:
    """Parse ZSim output file into a list of period dictionaries with caching."""
    # Check cache first
    if file_path in _PARSE_CACHE:
        return _PARSE_CACHE[file_path]
    
    start_time = time.time()
    
    with open(file_path, 'r') as f:
        content = f.read()

    # Split the content by period delimiter
    periods = content.split('===')
    periods = [p.strip() for p in periods if p.strip()]

    result = []
    for period in periods:
        # Create a root dictionary for this period
        period_dict = {}

        # Track the current hierarchy path and the current indentation level
        path_stack = []
        indent_stack = [-1]  # Start with -1 to handle any initial indent

        for line in period.split('\n'):
            if not line or line.startswith('...'):
                continue

            # Count leading spaces to determine indentation level
            indent_level = len(line) - len(line.lstrip())
            line = line.strip()

            # Handle section headers (ending with colon)
            if ':' in line:
                parts = line.split(':', 1)
                key = parts[0].strip()

                # Check if this is a value or a section header
                if len(parts) > 1 and '#' in parts[1] and not re.match(r'^\s*\d+\s*#', parts[1].strip()):
                    # This is a section header with a comment
                    is_value = False
                elif len(parts) > 1 and parts[1].strip():
                    # This has a value after the colon
                    is_value = True
                    value = parts[1].split('#')[0].strip()
                    try:
                        # Try to convert to numeric value
                        if '.' in value:
                            value = float(value)
                        else:
                            value = int(value)
                    except ValueError:
                        # Keep as string if not numeric
                        pass
                else:
                    # This is a section header
                    is_value = False

                # Pop higher or equal indentation levels from the stack
                while indent_stack and indent_level <= indent_stack[-1]:
                    indent_stack.pop()
                    if path_stack:
                        path_stack.pop()

                # If this is a new section, add to the stack
                if not is_value:
                    path_stack.append(key)
                    indent_stack.append(indent_level)

                    # Create the nested dictionaries as needed
                    current = period_dict
                    for p in path_stack[:-1]:
                        if p not in current:
                            current[p] = {}
                        current = current[p]
                    current[path_stack[-1]] = {}
                else:
                    # This is a value, add it to the current path
                    current = period_dict
                    for p in path_stack:
                        if p not in current:
                            current[p] = {}
                        current = current[p]
                    current[key] = value

        result.append(period_dict)
    
    # Cache the result
    _PARSE_CACHE[file_path] = result
    
    end_time = time.time()
    print(f"Parsed file in {end_time - start_time:.2f} seconds")
    
    return result


def get_multiple_values(data: List[Dict[str, Any]], paths: List[str]) -> Dict[str, List[Any]]:
    """
    Efficiently extract multiple values from parsed data in a single pass.
    
    Args:
        data: List of period dictionaries
        paths: List of dot-notation paths to extract
        
    Returns:
        Dictionary mapping paths to lists of values
    """
    result = {path: [] for path in paths}
    
    for period_dict in data:
        for path in paths:
            result[path].append(get_value_by_path(period_dict, path))
    
    return result


def get_value_by_path(data: Dict[str, Any], path: str) -> Union[int, float, str, None]:
    """Get a value from nested dictionary using a dot-notation path."""
    keys = path.split('.')
    current = data

    for key in keys:
        if key in current:
            current = current[key]
        else:
            # Try with exact match first
            found = False
            # Special case for hyphenated keys like "mem-0"
            if '-' in key:
                exact_key = key  # Try the exact key first
                if exact_key in current:
                    current = current[exact_key]
                    found = True

            # If not found, try other variations
            if not found:
                return None

    return current


def get_stats_series(file_path: str, target_path: str) -> List[Union[int, float, str, None]]:
    """Extract a series of values for a specific statistic across all periods."""
    periods = parse_zsim_output(file_path)
    return [get_value_by_path(period, target_path) for period in periods]


def calculate_average(values: List[Union[int, float]], start_idx: int = 0, end_idx: int = None) -> float:
    """Calculate the average of values in the specified range."""
    if end_idx is None:
        end_idx = len(values)
    
    # Use NumPy for efficient calculation if available
    if 'np' in globals():
        # Convert to numpy array and filter out None values
        valid_values = np.array([v for v in values[start_idx:end_idx] 
                                if v is not None and isinstance(v, (int, float))])
        if len(valid_values) == 0:
            return None
        return float(np.mean(valid_values))
    else:
        # Fallback to Python implementation
        numeric_values = [v for v in values[start_idx:end_idx]
                        if v is not None and isinstance(v, (int, float))]
        if not numeric_values:
            return None
        return sum(numeric_values) / len(numeric_values)


def calculate_cache_rate_trend(hits: List[Union[int, float, None]],
                               misses: List[Union[int, float, None]],
                               window_size: int = 1,
                               rate_type: str = 'miss') -> List[float]:
    """
    Calculate the cache miss or hit rate trend over periods.
    
    Args:
        hits: List of cache hit values for each period
        misses: List of cache miss values for each period
        window_size: Number of periods to average (default 1)
        rate_type: 'miss' to calculate miss rate, 'hit' to calculate hit rate
    
    Returns:
        List of miss rates or hit rates for each period (NaN if not calculable)
    """
    if len(hits) != len(misses):
        raise ValueError("Hit and miss lists must have the same length")

    # Use NumPy for efficiency if available
    if 'np' in globals():
        # Convert to numpy arrays, replacing None with NaN
        np_hits = np.array([float(h) if h is not None else np.nan for h in hits])
        np_misses = np.array([float(m) if m is not None else np.nan for m in misses])
        
        # Initialize result array
        rates = np.full(len(hits), np.nan)
        
        # Calculate rates for each position
        for i in range(len(hits)):
            if i >= window_size - 1:
                window_hits = np_hits[max(0, i - window_size + 1):i+1]
                window_misses = np_misses[max(0, i - window_size + 1):i+1]
                
                # Sum non-NaN values
                total_hits = np.nansum(window_hits)
                total_misses = np.nansum(window_misses)
                total = total_hits + total_misses
                
                if total > 0:
                    if rate_type.lower() == 'miss':
                        rates[i] = total_misses / total
                    else:  # hit rate
                        rates[i] = total_hits / total
        
        return rates.tolist()
    else:
        # Fallback to Python implementation
        rates = []
        
        # Ensure window_size is at least 1
        window_size = max(1, window_size)
        
        for i in range(len(hits)):
            if i < window_size - 1:
                # Not enough periods for a full window
                rates.append(float('nan'))
                continue
                
            # Collect window data
            window_hits = [hits[j] for j in range(i - window_size + 1, i + 1)
                        if hits[j] is not None and isinstance(hits[j], (int, float))]
            window_misses = [misses[j] for j in range(i - window_size + 1, i + 1)
                            if misses[j] is not None and isinstance(misses[j], (int, float))]
                
            # Sum up valid values in the window
            total_hits = sum(window_hits) if window_hits else 0
            total_misses = sum(window_misses) if window_misses else 0
            total = total_hits + total_misses
                
            if total == 0:
                # No data or division by zero
                rates.append(float('nan'))
            else:
                # Calculate the appropriate rate
                if rate_type.lower() == 'miss':
                    rates.append(total_misses / total)
                else:  # hit rate
                    rates.append(total_hits / total)
                    
        return rates


def calculate_ipc(file_path: str, base_path: str, window_size: int = 1):
    """
    Calculate Instructions Per Cycle (IPC) for each core and overall system.
    
    Args:
        file_path: Path to the zsim output file
        base_path: Base path prefix (e.g., "root.skylake") 
        window_size: Size of window for moving average smoothing
        
    Returns:
        Tuple of (per_core_ipc_dict, overall_ipc_list)
    """
    start_time = time.time()
    # Parse file only once
    periods = parse_zsim_output(file_path)
    
    # First identify all cores and create path queries
    max_core = 15  # Assume up to 16 cores (0-15)
    all_paths = []
    core_paths = {}
    
    for core_id in range(max_core + 1):
        core_name = f"{base_path.split('.')[-1]}-{core_id}"
        core_path = f"{base_path}.{core_name}"
        
        # Add paths for cycles, cCycles, and instrs
        cycles_path = f"{core_path}.cycles"
        cCycles_path = f"{core_path}.cCycles"
        instrs_path = f"{core_path}.instrs"
        
        all_paths.extend([cycles_path, cCycles_path, instrs_path])
        core_paths[core_id] = {
            'cycles': cycles_path,
            'cCycles': cCycles_path,
            'instrs': instrs_path
        }
    
    # Get all values in a single pass
    print("Extracting core statistics...")
    all_values = get_multiple_values(periods, all_paths)
    
    # Now organize data by core
    cores_data = {}
    for core_id, paths in core_paths.items():
        cores_data[core_id] = {
            'cycles': all_values[paths['cycles']],
            'cCycles': all_values[paths['cCycles']],
            'instrs': all_values[paths['instrs']]
        }
    
    print("Calculating IPC...")
    # Calculate IPC using NumPy if available
    if 'np' in globals():
        # Prepare arrays for each core
        ipc_data = {}
        per_period_data = []
        
        # Get number of periods
        num_periods = max(len(data['cycles']) for core_id, data in cores_data.items())
        
        # Initialize arrays for overall IPC calculation
        total_instrs_array = np.zeros(num_periods)
        total_cycles_array = np.zeros(num_periods)
        
        # Process each core
        for core_id, data in cores_data.items():
            # Convert to NumPy arrays, replacing None with 0
            cycles = np.array([float(c) if c is not None else 0.0 for c in data['cycles']])
            cCycles = np.array([float(c) if c is not None else 0.0 for c in data['cCycles']])
            instrs = np.array([float(i) if i is not None else 0.0 for i in data['instrs']])
            
            # Pad arrays if needed
            if len(cycles) < num_periods:
                cycles = np.pad(cycles, (0, num_periods - len(cycles)), mode='constant', constant_values=0)
            if len(cCycles) < num_periods:
                cCycles = np.pad(cCycles, (0, num_periods - len(cCycles)), mode='constant', constant_values=0)
            if len(instrs) < num_periods:
                instrs = np.pad(instrs, (0, num_periods - len(instrs)), mode='constant', constant_values=0)
            
            # Calculate IPC for this core
            total_cycles = cycles + cCycles
            # Avoid division by zero
            ipc = np.divide(instrs, total_cycles, out=np.zeros_like(instrs), where=total_cycles!=0)
            
            # Store in per-core dict
            ipc_data[core_id] = ipc.tolist()
            
            # Add to overall totals
            total_instrs_array += instrs
            total_cycles_array += total_cycles
            
            # Prepare period data for each core
            for i in range(num_periods):
                if i >= len(per_period_data):
                    per_period_data.append({
                        'period': i, 
                        'cores': {}, 
                        'overall_ipc': 0, 
                        'total_instrs': 0, 
                        'total_cycles': 0
                    })
                
                per_period_data[i]['cores'][core_id] = {
                    'cycles': cycles[i],
                    'cCycles': cCycles[i],
                    'instrs': instrs[i],
                    'ipc': ipc[i]
                }
        
        # Calculate overall IPC
        overall_ipc = np.divide(total_instrs_array, total_cycles_array, 
                              out=np.zeros_like(total_instrs_array), where=total_cycles_array!=0)
        
        # Update period data with overall metrics
        for i in range(num_periods):
            per_period_data[i]['overall_ipc'] = overall_ipc[i]
            per_period_data[i]['total_instrs'] = total_instrs_array[i]
            per_period_data[i]['total_cycles'] = total_cycles_array[i]
        
        # Apply window smoothing if needed
        if window_size > 1:
            # Smooth per-core IPC
            smoothed_ipc_data = {}
            for core_id, values in ipc_data.items():
                smoothed_ipc_data[core_id] = smooth_values_numpy(values, window_size)
            ipc_data = smoothed_ipc_data
            
            # Smooth overall IPC
            overall_ipc = smooth_values_numpy(overall_ipc.tolist(), window_size)
        else:
            overall_ipc = overall_ipc.tolist()
    else:
        # Fallback to Python implementation
        # Calculate IPC for each core and period
        ipc_data = {}
        per_period_data = []
        
        # Calculate total IPC across all periods
        num_periods = max(len(data['cycles']) for core_id, data in cores_data.items())
        
        for period in range(num_periods):
            total_instrs = 0
            total_cycles = 0
            period_dict = {'period': period, 'cores': {}}
            
            # Calculate IPC for each core in this period
            for core_id, data in cores_data.items():
                if period < len(data['cycles']) and period < len(data['cCycles']) and period < len(data['instrs']):
                    cycles_val = data['cycles'][period] if data['cycles'][period] is not None else 0
                    cCycles_val = data['cCycles'][period] if data['cCycles'][period] is not None else 0
                    instrs_val = data['instrs'][period] if data['instrs'][period] is not None else 0
                    
                    total_instrs += instrs_val
                    total_cycles += (cycles_val + cCycles_val)
                    
                    # Calculate IPC for this core
                    core_ipc = instrs_val / (cycles_val + cCycles_val) if (cycles_val + cCycles_val) > 0 else 0
                    
                    # Store in per-core dict
                    if core_id not in ipc_data:
                        ipc_data[core_id] = []
                    ipc_data[core_id].append(core_ipc)
                    
                    # Store in period dict
                    period_dict['cores'][core_id] = {
                        'cycles': cycles_val,
                        'cCycles': cCycles_val,
                        'instrs': instrs_val,
                        'ipc': core_ipc
                    }
            
            # Calculate overall IPC for this period
            period_ipc = total_instrs / total_cycles if total_cycles > 0 else 0
            period_dict['overall_ipc'] = period_ipc
            period_dict['total_instrs'] = total_instrs
            period_dict['total_cycles'] = total_cycles
            
            per_period_data.append(period_dict)
        
        # Extract overall IPC trend
        overall_ipc = [period['overall_ipc'] for period in per_period_data]
        
        # Apply window smoothing if needed
        if window_size > 1:
            smoothed_ipc_data = {}
            for core_id, values in ipc_data.items():
                if values:  # Only process cores with data
                    smoothed_ipc_data[core_id] = smooth_values(values, window_size)
            ipc_data = smoothed_ipc_data
            
            # Smooth overall IPC
            overall_ipc = smooth_values(overall_ipc, window_size)
    
    end_time = time.time()
    print(f"IPC calculation completed in {end_time - start_time:.2f} seconds")
    
    return ipc_data, overall_ipc, per_period_data


def smooth_values_numpy(values, window_size):
    """Apply a moving average smoothing to a list of values using NumPy."""
    if 'np' not in globals():
        return smooth_values(values, window_size)
    
    # Convert to numpy array, replacing None with NaN
    arr = np.array([float(v) if v is not None else np.nan for v in values])
    result = np.full_like(arr, np.nan)
    
    for i in range(len(arr)):
        window_start = max(0, i - window_size + 1)
        window = arr[window_start:i+1]
        # Use numpy's nanmean to handle NaN values
        if not np.all(np.isnan(window)):
            result[i] = np.nanmean(window)
    
    # Convert back to Python list, replacing NaN with None
    return [float(x) if not np.isnan(x) else None for x in result]


def smooth_values(values, window_size):
    """Apply a moving average smoothing to a list of values."""
    result = []
    for i in range(len(values)):
        window_start = max(0, i - window_size + 1)
        window = values[window_start:i+1]
        # Filter out None values
        valid_window = [v for v in window if v is not None]
        if valid_window:
            result.append(sum(valid_window) / len(valid_window))
        else:
            result.append(None)
    return result


def plot_cache_rate_trend(hit_rates: List[float], miss_rates: List[float], 
                          file_path: str, cache_name: str, window_size: int):
    """
    Plot hit rate and miss rate trends using seaborn.
    
    Args:
        hit_rates: List of hit rates
        miss_rates: List of miss rates
        file_path: Path to the output file for the plot
        cache_name: Name of the cache (extracted from the path)
        window_size: Window size used for smoothing
    """
    if not PLOTTING_AVAILABLE:
        print("Plotting requires matplotlib and seaborn. Please install with:")
        print("pip install matplotlib seaborn numpy pandas")
        return
    
    # Check if we have any valid data to plot
    if 'np' in globals():
        hit_array = np.array(hit_rates)
        miss_array = np.array(miss_rates)
        valid_hit_data = ~np.isnan(hit_array)
        valid_miss_data = ~np.isnan(miss_array)
        if not np.any(valid_hit_data) and not np.any(valid_miss_data):
            print("No valid data to plot. All values are NaN or None.")
            return
    else:
        valid_hit_data = [x for x in hit_rates if not math.isnan(x)]
        valid_miss_data = [x for x in miss_rates if not math.isnan(x)]
        if not valid_hit_data and not valid_miss_data:
            print("No valid data to plot. All values are NaN or None.")
            return

    try:
        # Set up the plot style
        sns.set_theme(style="whitegrid")
        plt.figure(figsize=(12, 6))

        # Import pandas here to ensure it's available
        import pandas as pd
        
        # Create data for plotting with explicit DataFrame construction
        plot_data = []
        
        for i in range(len(hit_rates)):
            if i < len(hit_rates) and not math.isnan(hit_rates[i]):
                plot_data.append({
                    'Period': i,
                    'Rate': hit_rates[i],
                    'Type': 'Hit Rate'
                })
                
        for i in range(len(miss_rates)):
            if i < len(miss_rates) and not math.isnan(miss_rates[i]):
                plot_data.append({
                    'Period': i,
                    'Rate': miss_rates[i],
                    'Type': 'Miss Rate'
                })
        
        # Create DataFrame if we have data
        if plot_data:
            df = pd.DataFrame(plot_data)
            
            # Plot the data
            sns.lineplot(data=df, x='Period', y='Rate', hue='Type', 
                        marker='o', linewidth=2, markersize=8)
            
            # Set plot title and labels
            plt.title(f"Cache Performance: {cache_name} (Window Size: {window_size})")
            plt.xlabel("Period")
            plt.ylabel("Rate")
            plt.ylim(-0.05, 1.05)  # Rate is between 0 and 1
            
            # Add horizontal grid lines
            plt.grid(axis='y', linestyle='--', alpha=0.7)
            
            # Get the output filename from the cache path
            cache_short_name = cache_name.split('.')[-1]
            output_filename = f"{cache_short_name}_trend_w{window_size}.png"
            
            # Save the plot
            plt.tight_layout()
            plt.savefig(output_filename)
            print(f"Plot saved to: {output_filename}")
            
            # Show the plot
            plt.show()
        else:
            print("No valid data points to plot after filtering NaN values.")
    except Exception as e:
        print(f"Error creating plot: {e}")
        print("This may be due to missing pandas or empty dataset.")


def plot_ipc_trend(ipc_data, overall_ipc, base_path, window_size):
    """
    Plot IPC trends for cores and overall system.
    
    Args:
        ipc_data: Dictionary mapping core_id to list of IPC values
        overall_ipc: List of overall system IPC values
        base_path: Base path string for title
        window_size: Window size used for smoothing
    """
    if not PLOTTING_AVAILABLE:
        print("Plotting requires matplotlib and seaborn. Please install with:")
        print("pip install matplotlib seaborn numpy pandas")
        return
    
    try:
        # Set up the plot style
        sns.set_theme(style="whitegrid")
        plt.figure(figsize=(14, 8))
        
        # Import pandas here to ensure it's available
        import pandas as pd
        
        # Create data for plotting
        plot_data = []
        
        # Add data for each active core
        for core_id, ipc_values in ipc_data.items():
            if any(ipc_values):  # Only include cores with data
                for period, ipc in enumerate(ipc_values):
                    if ipc is not None:
                        plot_data.append({
                            'Period': period,
                            'IPC': ipc,
                            'Core': f'Core {core_id}'
                        })
        
        # Add overall IPC data
        for period, ipc in enumerate(overall_ipc):
            if ipc is not None:
                plot_data.append({
                    'Period': period,
                    'IPC': ipc,
                    'Core': 'Overall'
                })
        
        # Create DataFrame if we have data
        if plot_data:
            df = pd.DataFrame(plot_data)
            
            # Plot the data
            sns.lineplot(data=df[df['Core'] != 'Overall'], x='Period', y='IPC', 
                        hue='Core', alpha=0.7, linewidth=1)
            
            # Highlight the overall IPC with a thicker black line
            if 'Overall' in df['Core'].values:
                sns.lineplot(data=df[df['Core'] == 'Overall'], x='Period', y='IPC',
                            color='black', linewidth=3, label='Overall')
            
            # Set plot title and labels
            plt.title(f"Instructions Per Cycle: {base_path.split('.')[-1]} (Window Size: {window_size})")
            plt.xlabel("Period")
            plt.ylabel("IPC")
            
            # Add grid
            plt.grid(axis='y', linestyle='--', alpha=0.7)
            
            # Get the output filename
            output_filename = f"ipc_trend_w{window_size}.png"
            
            # Save the plot
            plt.tight_layout()
            plt.savefig(output_filename)
            print(f"Plot saved to: {output_filename}")
            
            # Show the plot
            plt.show()
        else:
            print("No valid IPC data points to plot.")
    except Exception as e:
        print(f"Error creating IPC plot: {e}")
        print("This may be due to missing pandas or empty dataset.")


def main():
    if len(sys.argv) < 3:
        print(
            "Usage: python parse_stats.py <zsim_output_file> <target_path> [window_size] [plot]")
        sys.exit(1)

    start_time = time.time()
    file_path = sys.argv[1]
    target_path = sys.argv[2]
    window_size = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3].isdigit() else 1
    plot_enabled = "plot" in sys.argv if len(sys.argv) > 3 else False

    # Check if we should calculate IPC (if target is a processor like "root.skylake")
    if target_path.endswith("skylake") or "cpu" in target_path.lower():
        print(f"Calculating IPC for {target_path} with window size {window_size}")
        
        # Calculate IPC
        ipc_data, overall_ipc, period_data = calculate_ipc(file_path, target_path, window_size)
        
        # Print IPC for active cores
        print("\nPer-Core IPC:")
        for core_id, ipc_values in ipc_data.items():
            if any(ipc_values):  # Only print active cores
                valid_values = [v for v in ipc_values if v is not None]
                if valid_values:
                    avg_ipc = sum(valid_values) / len(valid_values)
                    print(f"  Core {core_id}: {avg_ipc:.4f}")
        
        # Print overall IPC
        valid_ipc = [ipc for ipc in overall_ipc if ipc is not None]
        if valid_ipc:
            avg_overall = sum(valid_ipc) / len(valid_ipc)
            print(f"\nOverall System IPC: {avg_overall:.4f}")
        
        # Plot if requested
        if plot_enabled:
            plot_ipc_trend(ipc_data, overall_ipc, target_path, window_size)

    # Check if we're calculating cache rates
    elif "loadHit" in target_path or "loadMiss" in target_path:
        # Remove the last component (hit or miss)
        base_path = target_path.rsplit('.', 1)[0]

        print(f"Analyzing cache performance for {base_path}")
        
        # Get both hit and miss stats in one pass
        hit_path = f"{base_path}.loadHit"
        miss_path = f"{base_path}.loadMiss"
        
        # Parse the file once and get both values
        all_data = parse_zsim_output(file_path)
        paths_to_extract = [hit_path, miss_path]
        extracted_values = get_multiple_values(all_data, paths_to_extract)
        
        hits = extracted_values[hit_path]
        misses = extracted_values[miss_path]

        print(f"Values for {hit_path}: {hits}")
        print(f"Values for {miss_path}: {misses}")

        hit_rates = calculate_cache_rate_trend(
            hits, misses, window_size, 'hit')
        miss_rates = calculate_cache_rate_trend(
            hits, misses, window_size, 'miss')

        print(f"\nHit rate trend (window={window_size}): {hit_rates}")
        print(f"Miss rate trend (window={window_size}): {miss_rates}")

        # Calculate averages ignoring NaN values
        if 'np' in globals():
            # NumPy implementation
            hit_array = np.array([float(x) if not math.isnan(x) else np.nan for x in hit_rates])
            miss_array = np.array([float(x) if not math.isnan(x) else np.nan for x in miss_rates])
            
            if not np.all(np.isnan(hit_array)):
                print(f"Average hit rate: {np.nanmean(hit_array):.4f}")
            if not np.all(np.isnan(miss_array)):
                print(f"Average miss rate: {np.nanmean(miss_array):.4f}")
        else:
            # Python implementation
            valid_hit_rates = [r for r in hit_rates if not math.isnan(r)]
            valid_miss_rates = [r for r in miss_rates if not math.isnan(r)]

            if valid_hit_rates:
                print(f"Average hit rate: {sum(valid_hit_rates)/len(valid_hit_rates):.4f}")
            if valid_miss_rates:
                print(f"Average miss rate: {sum(valid_miss_rates)/len(valid_miss_rates):.4f}")
        
        # Plot the trend if requested
        if plot_enabled:
            try:
                plot_cache_rate_trend(hit_rates, miss_rates, file_path, base_path, window_size)
            except Exception as e:
                print(f"Unable to create plot: {e}")
    else:
        # Regular path lookup
        values = get_stats_series(file_path, target_path)
        print(f"Values for {target_path}: {values}")

        avg = calculate_average(values)
        print(f"\nAverage: {avg}")

    end_time = time.time()
    print(f"Total execution time: {end_time - start_time:.2f} seconds")


if __name__ == "__main__":
    main()
