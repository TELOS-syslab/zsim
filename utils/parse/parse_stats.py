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
                             rate_type: str = 'miss',
                             step: int = 1) -> List[float]:
    """
    Calculate the cache miss or hit rate trend over periods, handling accumulated stats.
    
    Args:
        hits: List of hit counts
        misses: List of miss counts
        window_size: Size of the sliding window
        rate_type: Type of rate to calculate ('miss' or 'hit')
        step: Step size for calculating rates (stride)
    """
    if len(hits) != len(misses):
        raise ValueError("Hit and miss lists must have the same length")

    # Convert to numpy arrays
    hits_array = np.array([float(h) if h is not None else np.nan for h in hits])
    misses_array = np.array([float(m) if m is not None else np.nan for m in misses])
    
    # Calculate differences between consecutive periods
    hits_diff = np.diff(hits_array, prepend=0)
    misses_diff = np.diff(misses_array, prepend=0)
    
    # Initialize result array
    rates = np.full(len(hits), np.nan)
    
    # Calculate rates for each window with the given step size
    for i in range(0, len(hits), step):
        if i >= window_size - 1:
            window_hits = np.sum(hits_diff[max(0, i - window_size + 1):i+1])
            window_misses = np.sum(misses_diff[max(0, i - window_size + 1):i+1])
            total = window_hits + window_misses
            
            if total > 0:
                if rate_type.lower() == 'miss':
                    rates[i] = window_misses / total
                else:  # hit rate
                    rates[i] = window_hits / total
    
    return rates.tolist()


def calculate_total_hit_rate_trend(load_hits: List[Union[int, float, None]],
                                  load_misses: List[Union[int, float, None]],
                                  store_hits: List[Union[int, float, None]],
                                  store_misses: List[Union[int, float, None]],
                                  window_size: int = 1,
                                  step: int = 1) -> List[float]:
    """
    Calculate the total cache hit rate trend (loads + stores) over periods.
    
    Args:
        load_hits: List of load hit counts
        load_misses: List of load miss counts
        store_hits: List of store hit counts
        store_misses: List of store miss counts
        window_size: Size of the sliding window
        step: Step size for calculating rates (stride)
    """
    if len(load_hits) != len(load_misses) or len(load_hits) != len(store_hits) or len(load_hits) != len(store_misses):
        raise ValueError("Hit and miss lists must have the same length")

    # Convert to numpy arrays
    load_hits_array = np.array([float(h) if h is not None else np.nan for h in load_hits])
    load_misses_array = np.array([float(m) if m is not None else np.nan for m in load_misses])
    store_hits_array = np.array([float(h) if h is not None else np.nan for h in store_hits])
    store_misses_array = np.array([float(m) if m is not None else np.nan for m in store_misses])
    
    # Calculate differences between consecutive periods
    load_hits_diff = np.diff(load_hits_array, prepend=0)
    load_misses_diff = np.diff(load_misses_array, prepend=0)
    store_hits_diff = np.diff(store_hits_array, prepend=0)
    store_misses_diff = np.diff(store_misses_array, prepend=0)
    
    # Initialize result array
    rates = np.full(len(load_hits), np.nan)
    
    # Calculate rates for each window with the given step size
    for i in range(0, len(load_hits), step):
        if i >= window_size - 1:
            window_load_hits = np.sum(load_hits_diff[max(0, i - window_size + 1):i+1])
            window_load_misses = np.sum(load_misses_diff[max(0, i - window_size + 1):i+1])
            window_store_hits = np.sum(store_hits_diff[max(0, i - window_size + 1):i+1])
            window_store_misses = np.sum(store_misses_diff[max(0, i - window_size + 1):i+1])
            
            total_hits = window_load_hits + window_store_hits
            total_accesses = total_hits + window_load_misses + window_store_misses
            
            if total_accesses > 0:
                rates[i] = total_hits / total_accesses
    
    return rates.tolist()


def calculate_ipc(file_path: str, base_path: str, window_size: int = 1):
    """Calculate Instructions Per Cycle (IPC) for each core and overall system."""
    start_time = time.time()
    
    # Parse file and find core paths
    data = parse_zsim_output(file_path)
    core_paths = find_core_paths(data)
    
    if not core_paths:
        print("No core paths found")
        return {}, [], []
    
    print(f"Found {len(core_paths)} cores:")
    for path in core_paths:
        print(f"  {path}")
    
    # Get all core stats in one pass
    paths_to_extract = []
    for core_path in core_paths:
        paths_to_extract.extend([
            f"{core_path}.cycles",
            f"{core_path}.cCycles",
            f"{core_path}.instrs"
        ])
    
    extracted_values = get_multiple_values(data, paths_to_extract)
    
    # Process each core's data
    ipc_data = {}
    total_instrs_by_period = np.zeros(len(data))
    total_cycles_by_period = np.zeros(len(data))
    
    for core_path in core_paths:
        cycles = np.array([float(c) if c is not None else np.nan for c in extracted_values[f"{core_path}.cycles"]])
        cCycles = np.array([float(c) if c is not None else np.nan for c in extracted_values[f"{core_path}.cCycles"]])
        instrs = np.array([float(i) if i is not None else np.nan for i in extracted_values[f"{core_path}.instrs"]])
        
        # Calculate differences between consecutive periods
        cycles_diff = np.diff(cycles, prepend=0)
        cCycles_diff = np.diff(cCycles, prepend=0)
        instrs_diff = np.diff(instrs, prepend=0)
        
        # Calculate IPC for each window
        core_ipc = np.full(len(cycles), np.nan)
        for i in range(len(cycles)):
            if i >= window_size - 1:
                window_cycles = np.sum(cycles_diff[max(0, i - window_size + 1):i+1])
                window_cCycles = np.sum(cCycles_diff[max(0, i - window_size + 1):i+1])
                window_instrs = np.sum(instrs_diff[max(0, i - window_size + 1):i+1])
                
                total_cycles = window_cycles + window_cCycles
                if total_cycles > 0:
                    core_ipc[i] = window_instrs / total_cycles
        
        core_name = core_path.split('.')[-1]
        ipc_data[core_name] = core_ipc.tolist()
        
        # Add to totals for overall IPC
        total_instrs_by_period += instrs_diff
        total_cycles_by_period += (cycles_diff + cCycles_diff)
    
    # Calculate overall IPC for each window
    overall_ipc = np.full(len(data), np.nan)
    for i in range(len(data)):
        if i >= window_size - 1:
            window_instrs = np.sum(total_instrs_by_period[max(0, i - window_size + 1):i+1])
            window_cycles = np.sum(total_cycles_by_period[max(0, i - window_size + 1):i+1])
            if window_cycles > 0:
                overall_ipc[i] = window_instrs / window_cycles
    
    end_time = time.time()
    print(f"IPC calculation completed in {end_time - start_time:.2f} seconds")
    
    return ipc_data, overall_ipc.tolist(), []


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


def get_output_name(zsim_dir: str, cache_name: str = None, stat_type: str = None, 
                   window_size: int = 1, step: int = 1) -> str:
    """
    Generate output filename based on directory name pattern.
    
    Args:
        zsim_dir: Directory containing zsim output
        cache_name: Name of the cache
        stat_type: Type of statistic (hit, miss, ipc)
        window_size: Size of the sliding window
        step: Step size for data points
    """
    # Extract date and category from directory name
    dir_name = os.path.basename(zsim_dir)
    match = re.match(r'(\d{8}-\d{6})\[(.*?)\]', dir_name)
    if not match:
        return f"unknown_{stat_type}_w{window_size}_s{step}.png"
        
    date, category = match.groups()
    
    # Get the cache type from category (e.g., idealFullyCache from idealfully-cc-ddr)
    cache_type = None
    if '-' in category:
        parts = category.split('-')
        if len(parts) > 1:
            if 'cache' in parts[0].lower():
                cache_type = parts[0]
            else:
                cache_type = parts[0].capitalize() + 'Cache'
    
    if not cache_type:
        cache_type = 'UnknownCache'
    
    if stat_type.lower() in ['hit', 'miss']:
        # For cache stats, use cache name and category
        return f"[{category}]-hit-w{window_size}-s{step}-{cache_name}_{date}.png"
    elif stat_type.lower() == 'ipc':
        # For IPC stats, use same pattern as cache stats
        return f"[{category}]-ipc-w{window_size}-s{step}-{cache_name}_{date}.png"
    else:
        return f"[{category}]-{stat_type}-w{window_size}-s{step}-{cache_name}_{date}.png"


def setup_plot_style():
    """Setup publication-quality plot style."""
    sns.set_style("whitegrid", {
        'grid.linestyle': ':',
        'grid.color': '#cccccc',
        'grid.alpha': 0.5,
        'axes.edgecolor': '#000000',
        'axes.linewidth': 1.0,
    })
    
    # Set figure-wide parameters
    plt.rcParams.update({
        'figure.figsize': (10, 6),
        'figure.dpi': 300,
        'savefig.dpi': 300,
        'font.size': 12,
        'axes.titlesize': 14,
        'axes.labelsize': 12,
        'xtick.labelsize': 10,
        'ytick.labelsize': 10,
        'legend.fontsize': 10,
        'lines.linewidth': 1.5,
        'lines.markersize': 4,
        'lines.markeredgewidth': 1.0,
    })


def save_plot_data(data_dict: Dict, output_filename: str, plot_path: str):
    """Save plot data alongside the plot image."""
    data_path = os.path.join(plot_path, output_filename.replace('.png', '.npz'))
    np.savez(data_path, **data_dict)


def plot_cache_rate_trend(read_hit_rates: List[float], read_miss_rates: List[float], 
                         total_hit_rates: List[float],
                         zsim_dir: str, cache_name: str, window_size: int, plot_path: str,
                         step: int = 1):
    """Plot hit rate and miss rate trends using publication-quality style."""
    try:
        setup_plot_style()
        
        fig = plt.figure(figsize=(12, 6))
        ax = fig.add_subplot(111)
        
        # Create x-axis values with step size
        x = np.arange(0, len(read_hit_rates), step)
        
        # Filter data points according to step size
        read_hit_rates_stepped = [read_hit_rates[i] for i in range(0, len(read_hit_rates), step)]
        total_hit_rates_stepped = [total_hit_rates[i] for i in range(0, len(total_hit_rates), step)]
        
        # Plot hit and miss rates
        read_hit_line = ax.plot(x, read_hit_rates_stepped, 'o-', color='#1f77b4', 
                               label='Read Hit Rate', markerfacecolor='white', 
                               markeredgewidth=0.8, markersize=4)
        total_hit_line = ax.plot(x, total_hit_rates_stepped, 's-', color='#2ca02c', 
                                label='Total Hit Rate', markerfacecolor='white', 
                                markeredgewidth=0.8, markersize=4)
        
        # Rest of the plotting code remains the same
        ax.set_xlabel('Period')
        ax.set_ylabel('Rate')
        ax.set_title(f'Cache Performance: {cache_name}\n(Window Size: {window_size}, Step: {step})')
        ax.set_ylim(-0.02, 1.02)
        ax.grid(True, linestyle=':', alpha=0.5, color='#cccccc')
        
        box = ax.get_position()
        ax.set_position([box.x0, box.y0, box.width * 0.85, box.height])
        ax.legend(loc='center left', bbox_to_anchor=(1.05, 0.5),
                frameon=True, edgecolor='black', fancybox=False)
        
        output_filename = get_output_name(zsim_dir, cache_name=cache_name, 
                                        stat_type='hit', window_size=window_size,
                                        step=step)
        
        if not os.path.exists(plot_path):
            os.makedirs(plot_path)
        plt.savefig(os.path.join(plot_path, output_filename), bbox_inches='tight', pad_inches=0.1)
        
        # Save the actual data points
        save_plot_data({
            'read_hit_rates': read_hit_rates_stepped,
            'read_miss_rates': read_miss_rates,
            'total_hit_rates': total_hit_rates_stepped,
            'x': x
        }, output_filename, plot_path)
        
        print(f"Plot saved to: {output_filename}")
        plt.show()
        plt.close()
        
    except Exception as e:
        print(f"Error creating plot: {e}")


def plot_ipc_trend(ipc_data, overall_ipc, zsim_dir, window_size, plot_path):
    """Plot IPC trends with publication-quality style."""
    try:
        # Setup publication style
        setup_plot_style()
        
        # Create figure with extra space for legend
        fig = plt.figure(figsize=(12, 6))
        ax = fig.add_subplot(111)
        
        # Create color palette for cores
        n_cores = len(ipc_data)
        colors = plt.cm.tab20(np.linspace(0, 1, n_cores))
        
        # Plot each core's IPC with markers
        x = np.arange(len(next(iter(ipc_data.values()))))
        for (core_name, ipc_values), color in zip(ipc_data.items(), colors):
            ax.plot(x, ipc_values, '-', color=color, label=core_name, alpha=0.7,
                   marker='.', markersize=3, markeredgewidth=0)
        
        # Plot overall IPC with thick black line
        ax.plot(x, overall_ipc, '-', color='black', label='Overall',
                linewidth=2, zorder=n_cores+1)
        
        # Extract category from directory name for the title
        dir_name = os.path.basename(plot_path)
        match = re.match(r'\d{8}-\d{6}\[(.*?)\]', dir_name)
        category = match.group(1) if match else "unknown"
        
        # Customize the plot
        ax.set_xlabel('Period')
        ax.set_ylabel('Instructions Per Cycle (IPC)')
        ax.set_title(f'Instructions Per Cycle: {category}\n(Window Size: {window_size})')
        
        # Add grid with dotted lines
        ax.grid(True, linestyle=':', alpha=0.5, color='#cccccc')
        
        # Move legend outside to the right
        box = ax.get_position()
        ax.set_position([box.x0, box.y0, box.width * 0.85, box.height])
        ax.legend(loc='center left', bbox_to_anchor=(1.05, 0.5),
                 frameon=True, edgecolor='black', fancybox=False,
                 ncol=1 if n_cores <= 8 else 2)
        
        # Save both the plot and the data
        if not os.path.exists(plot_path):
            os.makedirs(plot_path)
        output_filename = get_output_name(zsim_dir, cache_name="system", stat_type='ipc', window_size=window_size, step=1)
        plt.savefig(os.path.join(plot_path, output_filename), bbox_inches='tight', pad_inches=0.1)
        
        # Save the actual data points
        save_plot_data({
            'ipc_data': ipc_data,
            'overall_ipc': overall_ipc,
            'x': x
        }, output_filename, plot_path)
        
        print(f"Plot saved to: {output_filename}")
        plt.show()
        plt.close()
        
    except Exception as e:
        print(f"Error creating IPC plot: {e}")


def find_cache_paths(data: List[Dict[str, Any]], base_path: str = "root.mem.mem-0") -> List[str]:
    """Find all cache paths under the given base path."""
    if not data:
        return []
    
    # Try each period until we find valid data
    for period in data:
        if not period:  # Skip empty periods
            continue
            
        # Navigate to mem-0
        current = period
        try:
            for part in base_path.split('.'):
                if part in current:
                    current = current[part]
                else:
                    continue  # Try next period if path not found
                    
            # Find first child that has loadHit/loadMiss
            cache_paths = []
            for key in current.keys():
                if isinstance(current[key], dict) and 'loadHit' in current[key]:
                    cache_paths.append(f"{base_path}.{key}")
            
            if cache_paths:  # If we found any cache paths, return them
                return cache_paths
        except (KeyError, AttributeError):
            continue  # Try next period if any error occurs
    
    return []  # Return empty list if no cache paths found in any period


def find_core_paths(data: List[Dict[str, Any]]) -> List[str]:
    """Find all core paths that have cycles, cCycles, and instrs stats."""
    if not data:
        return []
    
    core_paths = set()  # Use set to avoid duplicates
    
    def search_dict(d: Dict[str, Any], current_path: str):
        if not isinstance(d, dict):
            return
            
        for key, value in d.items():
            if not isinstance(value, dict):
                continue
                
            new_path = f"{current_path}.{key}" if current_path else key
            
            # Check if this is a core stats section
            if ('cycles' in value and 'cCycles' in value and 'instrs' in value) or \
               any(comment.strip() == '# Core stats' for comment in str(value).split('\n')):
                core_paths.add(new_path)
            else:
                search_dict(value, new_path)
    
    # Try each period until we find all core paths
    for period in data:
        if not period:  # Skip empty periods
            continue
            
        try:
            search_dict(period, "")
            if core_paths:  # If we found any paths, keep searching for more
                continue
        except (KeyError, AttributeError):
            continue  # Try next period if any error occurs
    
    return sorted(list(core_paths))  # Return sorted list of unique paths


def combine_plots(plots_dir: str):
    """Combine all plots in a directory into comparison graphs."""
    # Create plots directory if it doesn't exist
    os.makedirs(plots_dir, exist_ok=True)
    
    # Get all data files
    data_files = [f for f in os.listdir(plots_dir) if f.endswith('.npz')]
    
    # Group plots by type and window size
    hit_plots = {}  # window_size -> {category -> (cache_name, date, data)}
    ipc_plots = {}  # window_size -> {category -> (date, data)}
    
    for data_file in data_files:
        # Parse filename: [category]-type-w{window}-s{step}-{cache}_{date}.npz
        match = re.match(r'\[(.*?)\]-(hit|ipc)-w(\d+)-s(\d+)-(.+?)_(\d{8}-\d{6})\.npz', data_file)
        if not match:
            continue
            
        category, plot_type, window_size, step_size, cache_name, date = match.groups()
        window_size = int(window_size)
        step_size = int(step_size)
        
        try:
            # Load the data
            data = np.load(os.path.join(plots_dir, data_file))
            
            if plot_type == 'hit':
                if window_size not in hit_plots:
                    hit_plots[window_size] = {}
                hit_plots[window_size][category] = (cache_name, date, data, step_size)
            else:  # ipc
                if window_size not in ipc_plots:
                    ipc_plots[window_size] = {}
                ipc_plots[window_size][category] = (date, data, step_size)
        except Exception as e:
            print(f"Error loading {data_file}: {e}")
            continue
    
    # Extract benchmark name from category
    def get_benchmark(category):
        # Split by underscore first
        parts = category.split('_')
        if len(parts) <= 1:
            return category  # If there's no underscore, use the whole category
        
        # Skip the first part and join the last 1 or 2 parts
        remaining_parts = parts[1:]  # Skip first part
        return '_'.join(remaining_parts)
    
    # Process each window size
    for window_size in sorted(set(list(hit_plots.keys()) + list(ipc_plots.keys()))):
        # Plot hit rates if we have any
        if window_size in hit_plots and hit_plots[window_size]:
            # Get step size from first plot in the group
            first_category = next(iter(hit_plots[window_size]))
            _, _, _, step_size = hit_plots[window_size][first_category]
            
            # Read Hit Rate combined plot
            setup_plot_style()
            fig = plt.figure(figsize=(12, 6))
            ax = fig.add_subplot(111)
            
            # Plot each category's read hit rate
            colors = plt.cm.tab20(np.linspace(0, 1, len(hit_plots[window_size])))
            for (category, (cache_name, date, data, _)), color in zip(hit_plots[window_size].items(), colors):
                hit_rates = data['read_hit_rates'] if 'read_hit_rates' in data else data['hit_rates']
                x = data['x']
                
                ax.plot(x, hit_rates, '-', label=category, color=color, alpha=0.7)
            
            ax.set_xlabel('Period')
            ax.set_ylabel('Read Hit Rate')
            ax.set_title(f'Cache Read Hit Rate Comparison\n(Window Size: {window_size}, Step: {step_size})')
            ax.set_ylim(-0.02, 1.02)
            ax.grid(True, linestyle=':', alpha=0.5, color='#cccccc')
            
            # Move legend outside
            box = ax.get_position()
            ax.set_position([box.x0, box.y0, box.width * 0.85, box.height])
            ax.legend(loc='center left', bbox_to_anchor=(1.05, 0.5),
                     frameon=True, edgecolor='black', fancybox=False)
            
            plt.savefig(os.path.join(plots_dir, f'combined_rhit_w{window_size}_s{step_size}.png'),
                       bbox_inches='tight', pad_inches=0.1)
            plt.close()
            
            # Total Hit Rate combined plot
            setup_plot_style()
            fig = plt.figure(figsize=(12, 6))
            ax = fig.add_subplot(111)
            
            # Plot each category's total hit rate
            for (category, (cache_name, date, data, _)), color in zip(hit_plots[window_size].items(), colors):
                if 'total_hit_rates' in data:
                    total_hit_rates = data['total_hit_rates']
                    x = data['x']
                    
                    ax.plot(x, total_hit_rates, '-', label=category, color=color, alpha=0.7)
            
            ax.set_xlabel('Period')
            ax.set_ylabel('Total Hit Rate')
            ax.set_title(f'Cache Total Hit Rate Comparison\n(Window Size: {window_size}, Step: {step_size})')
            ax.set_ylim(-0.02, 1.02)
            ax.grid(True, linestyle=':', alpha=0.5, color='#cccccc')
            
            # Move legend outside
            box = ax.get_position()
            ax.set_position([box.x0, box.y0, box.width * 0.85, box.height])
            ax.legend(loc='center left', bbox_to_anchor=(1.05, 0.5),
                     frameon=True, edgecolor='black', fancybox=False)
            
            plt.savefig(os.path.join(plots_dir, f'combined_thit_w{window_size}_s{step_size}.png'),
                       bbox_inches='tight', pad_inches=0.1)
            plt.close()
            
            # Group by benchmark for part-combined graphs
            benchmark_groups = {}
            for category in hit_plots[window_size]:
                benchmark = get_benchmark(category)
                if benchmark not in benchmark_groups:
                    benchmark_groups[benchmark] = []
                benchmark_groups[benchmark].append(category)
            
            # Create part-combined plots for each benchmark - Read Hit Rate
            for benchmark, categories in benchmark_groups.items():
                if len(categories) <= 1:
                    continue  # Skip benchmarks with only one category
                
                setup_plot_style()
                fig = plt.figure(figsize=(12, 6))
                ax = fig.add_subplot(111)
                
                benchmark_colors = plt.cm.tab10(np.linspace(0, 1, len(categories)))
                for category, color in zip(categories, benchmark_colors):
                    cache_name, date, data, _ = hit_plots[window_size][category]
                    hit_rates = data['read_hit_rates'] if 'read_hit_rates' in data else data['hit_rates']
                    x = data['x']
                    
                    ax.plot(x, hit_rates, '-', label=category, color=color, alpha=0.7)
                
                ax.set_xlabel('Period')
                ax.set_ylabel('Read Hit Rate')
                ax.set_title(f'Cache Read Hit Rate Comparison for {benchmark}\n(Window Size: {window_size}, Step: {step_size})')
                ax.set_ylim(-0.02, 1.02)
                ax.grid(True, linestyle=':', alpha=0.5, color='#cccccc')
                
                # Move legend outside
                box = ax.get_position()
                ax.set_position([box.x0, box.y0, box.width * 0.85, box.height])
                ax.legend(loc='center left', bbox_to_anchor=(1.05, 0.5),
                         frameon=True, edgecolor='black', fancybox=False)
                
                plt.savefig(os.path.join(plots_dir, f'benchmark_{benchmark}_rhit_w{window_size}_s{step_size}.png'),
                           bbox_inches='tight', pad_inches=0.1)
                plt.close()
                
                # Create part-combined plots for each benchmark - Total Hit Rate
                setup_plot_style()
                fig = plt.figure(figsize=(12, 6))
                ax = fig.add_subplot(111)
                
                has_data = False
                for category, color in zip(categories, benchmark_colors):
                    cache_name, date, data, _ = hit_plots[window_size][category]
                    if 'total_hit_rates' in data:
                        total_hit_rates = data['total_hit_rates']
                        x = data['x']
                        
                        ax.plot(x, total_hit_rates, '-', label=category, color=color, alpha=0.7)
                        has_data = True
                
                if has_data:
                    ax.set_xlabel('Period')
                    ax.set_ylabel('Total Hit Rate')
                    ax.set_title(f'Cache Total Hit Rate Comparison for {benchmark}\n(Window Size: {window_size}, Step: {step_size})')
                    ax.set_ylim(-0.02, 1.02)
                    ax.grid(True, linestyle=':', alpha=0.5, color='#cccccc')
                    
                    # Move legend outside
                    box = ax.get_position()
                    ax.set_position([box.x0, box.y0, box.width * 0.85, box.height])
                    ax.legend(loc='center left', bbox_to_anchor=(1.05, 0.5),
                             frameon=True, edgecolor='black', fancybox=False)
                    
                    plt.savefig(os.path.join(plots_dir, f'benchmark_{benchmark}_thit_w{window_size}_s{step_size}.png'),
                               bbox_inches='tight', pad_inches=0.1)
                    plt.close()
        
        # Plot IPC if we have any
        if window_size in ipc_plots and ipc_plots[window_size]:
            # Get step size from first plot in the group
            first_category = next(iter(ipc_plots[window_size]))
            _, _, step_size = ipc_plots[window_size][first_category]
            
            setup_plot_style()
            fig = plt.figure(figsize=(12, 6))
            ax = fig.add_subplot(111)
            
            # Plot each category
            colors = plt.cm.tab20(np.linspace(0, 1, len(ipc_plots[window_size])))
            for (category, (date, data, _)), color in zip(ipc_plots[window_size].items(), colors):
                ipc_values = data['overall_ipc']
                x = data['x']
                
                ax.plot(x, ipc_values, '-', label=category, color=color, alpha=0.7)
            
            ax.set_xlabel('Period')
            ax.set_ylabel('Instructions Per Cycle (IPC)')
            ax.set_title(f'IPC Comparison\n(Window Size: {window_size}, Step: {step_size})')
            ax.grid(True, linestyle=':', alpha=0.5, color='#cccccc')
            
            # Move legend outside
            box = ax.get_position()
            ax.set_position([box.x0, box.y0, box.width * 0.85, box.height])
            ax.legend(loc='center left', bbox_to_anchor=(1.05, 0.5),
                     frameon=True, edgecolor='black', fancybox=False)
            
            plt.savefig(os.path.join(plots_dir, f'combined_ipc_w{window_size}_s{step_size}.png'),
                       bbox_inches='tight', pad_inches=0.1)
            plt.close()
            
            # Group by benchmark for part-combined graphs
            benchmark_groups = {}
            for category in ipc_plots[window_size]:
                benchmark = get_benchmark(category)
                if benchmark not in benchmark_groups:
                    benchmark_groups[benchmark] = []
                benchmark_groups[benchmark].append(category)
            
            # Create part-combined plot for each benchmark
            for benchmark, categories in benchmark_groups.items():
                if len(categories) <= 1:
                    continue  # Skip benchmarks with only one category
                
                setup_plot_style()
                fig = plt.figure(figsize=(12, 6))
                ax = fig.add_subplot(111)
                
                benchmark_colors = plt.cm.tab10(np.linspace(0, 1, len(categories)))
                for category, color in zip(categories, benchmark_colors):
                    date, data, _ = ipc_plots[window_size][category]
                    ipc_values = data['overall_ipc']
                    x = data['x']
                    
                    ax.plot(x, ipc_values, '-', label=category, color=color, alpha=0.7)
                
                ax.set_xlabel('Period')
                ax.set_ylabel('Instructions Per Cycle (IPC)')
                ax.set_title(f'IPC Comparison for {benchmark}\n(Window Size: {window_size}, Step: {step_size})')
                ax.grid(True, linestyle=':', alpha=0.5, color='#cccccc')
                
                # Move legend outside
                box = ax.get_position()
                ax.set_position([box.x0, box.y0, box.width * 0.85, box.height])
                ax.legend(loc='center left', bbox_to_anchor=(1.05, 0.5),
                         frameon=True, edgecolor='black', fancybox=False)
                
                plt.savefig(os.path.join(plots_dir, f'benchmark_{benchmark}_ipc_w{window_size}_s{step_size}.png'),
                           bbox_inches='tight', pad_inches=0.1)
                plt.close()


def main():
    if len(sys.argv) < 2:
        print("Usage: python parse_stats.py <zsim_output_path> [stat_type] [window_size] [step] [plot] [verbose]")
        sys.exit(1)

    start_time = time.time()
    zsim_dir = sys.argv[1]
    zsim_file = os.path.join(zsim_dir, "zsim-pout.out")
    
    plot_path = os.path.join(zsim_dir, "..", "plots")
    stat_type = sys.argv[2] if len(sys.argv) > 2 else "hit"
    window_size = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3].isdigit() else 1
    step = int(sys.argv[4]) if len(sys.argv) > 4 and sys.argv[4].isdigit() else 1
    plot_enabled = "plot" in sys.argv if len(sys.argv) > 4 else False
    verbose = "verbose" in sys.argv if len(sys.argv) > 5 else False
    
    # Check if we're calculating cache rates with just "hit" or "miss"
    if stat_type.lower() in ["hit", "miss", "thit"]:
        # Parse file once to find cache paths
        if not os.path.exists(zsim_file):
            print(f"Error: zsim-pout.out not found in directory {zsim_dir}")
            sys.exit(1)
        
        data = parse_zsim_output(zsim_file)
        cache_paths = find_cache_paths(data)
        
        if not cache_paths:
            print("No cache paths found under root.mem.mem-0")
            sys.exit(1)
            
        # Use the first cache found
        base_path = cache_paths[0]
        cache_name = base_path.split('.')[-1]
        print(f"Found cache: {cache_name}")
        
        # Get all relevant cache access stats
        load_hit_path = f"{base_path}.loadHit"
        load_miss_path = f"{base_path}.loadMiss"
        store_hit_path = f"{base_path}.storeHit"
        store_miss_path = f"{base_path}.storeMiss"
        
        print(f"Analyzing cache performance for {base_path}")
        
        # Get both load and store hits/misses 
        paths_to_extract = [load_hit_path, load_miss_path, store_hit_path, store_miss_path]
        extracted_values = get_multiple_values(data, paths_to_extract)
        
        load_hits = extracted_values[load_hit_path]
        load_misses = extracted_values[load_miss_path]
        store_hits = extracted_values[store_hit_path]
        store_misses = extracted_values[store_miss_path]

        # Calculate rates with step parameter
        read_hit_rates = calculate_cache_rate_trend(load_hits, load_misses, window_size, 'hit', step)
        read_miss_rates = calculate_cache_rate_trend(load_hits, load_misses, window_size, 'miss', step)
        total_hit_rates = calculate_total_hit_rate_trend(load_hits, load_misses, store_hits, store_misses, window_size, step)

        if verbose:
            print(f"\nRead hit rate trend (window={window_size}, step={step}): {read_hit_rates}")
            print(f"Read miss rate trend (window={window_size}, step={step}): {read_miss_rates}")
            print(f"Total hit rate trend (window={window_size}, step={step}): {total_hit_rates}")

        # Calculate averages ignoring NaN values
        if 'np' in globals():
            read_hit_array = np.array([float(x) if not math.isnan(x) else np.nan for x in read_hit_rates])
            read_miss_array = np.array([float(x) if not math.isnan(x) else np.nan for x in read_miss_rates])
            total_hit_array = np.array([float(x) if not math.isnan(x) else np.nan for x in total_hit_rates])
            
            if not np.all(np.isnan(read_hit_array)):
                print(f"Average read hit rate: {np.nanmean(read_hit_array):.4f}")
            if not np.all(np.isnan(read_miss_array)):
                print(f"Average read miss rate: {np.nanmean(read_miss_array):.4f}")
            if not np.all(np.isnan(total_hit_array)):
                print(f"Average total hit rate: {np.nanmean(total_hit_array):.4f}")
        else:
            valid_read_hit_rates = [r for r in read_hit_rates if not math.isnan(r)]
            valid_read_miss_rates = [r for r in read_miss_rates if not math.isnan(r)]
            valid_total_hit_rates = [r for r in total_hit_rates if not math.isnan(r)]

            if valid_read_hit_rates:
                print(f"Average read hit rate: {sum(valid_read_hit_rates)/len(valid_read_hit_rates):.4f}")
            if valid_read_miss_rates:
                print(f"Average read miss rate: {sum(valid_read_miss_rates)/len(valid_read_miss_rates):.4f}")
            if valid_total_hit_rates:
                print(f"Average total hit rate: {sum(valid_total_hit_rates)/len(valid_total_hit_rates):.4f}")
        
        # Plot with step parameter
        if plot_enabled:
            try:
                plot_cache_rate_trend(read_hit_rates, read_miss_rates, total_hit_rates, 
                                    zsim_dir, cache_name, window_size, plot_path, step)
            except Exception as e:
                print(f"Unable to create plot: {e}")

    # Check if we should calculate IPC
    elif stat_type.lower() == "ipc":
        # Calculate IPC using the updated function
        if not os.path.exists(zsim_file):
            print(f"Error: zsim-pout.out not found in directory {zsim_dir}")
            sys.exit(1)
        ipc_data, overall_ipc, _ = calculate_ipc(zsim_file, "root", window_size)
        
        if not ipc_data:
            print("No core paths found with cycles, cCycles, and instrs stats")
            sys.exit(1)
        
        # Print IPC for active cores
        print("\nPer-Core IPC:")
        for core_name, ipc_values in ipc_data.items():
            valid_values = [v for v in ipc_values if not np.isnan(v)]
            if valid_values:
                avg_ipc = np.mean(valid_values)
                print(f"  {core_name}: {avg_ipc:.4f}")
        
        # Print overall IPC
        valid_ipc = [ipc for ipc in overall_ipc if not np.isnan(ipc)]
        if valid_ipc:
            avg_overall = np.mean(valid_ipc)
            print(f"\nOverall System IPC: {avg_overall:.4f}")
        
        # Plot if requested
        if plot_enabled:
            plot_ipc_trend(ipc_data, overall_ipc, zsim_dir, window_size, plot_path)

    elif stat_type.lower() == "combine":
        plot_path = os.path.join(zsim_dir, "plots")
        combine_plots(plot_path)

    else:
        # Regular path lookup
        values = get_stats_series(zsim_file, stat_type)
        print(f"Values for {stat_type}: {values}")

        avg = calculate_average(values)
        print(f"\nAverage: {avg}")

    end_time = time.time()
    print(f"Total execution time: {end_time - start_time:.2f} seconds")


if __name__ == "__main__":
    main()
