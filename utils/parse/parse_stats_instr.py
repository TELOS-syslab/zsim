#!/usr/bin/env python3

import re
import sys
import math
import os
import time
from typing import List, Dict, Any, Union, Tuple, Optional
from functools import lru_cache
import h5py
import numpy as np
import argparse

# Add imports for plotting and efficient numerical operations
try:
    import matplotlib.pyplot as plt
    import seaborn as sns
    PLOTTING_AVAILABLE = True
except ImportError:
    PLOTTING_AVAILABLE = False

# Cache to avoid re-parsing the same file multiple times
_PARSE_CACHE = {}

# Add at the top of the file, after imports
VERBOSE = False  # Global verbose flag

def set_verbose(value: bool):
    """Set the global verbose flag."""
    global VERBOSE
    VERBOSE = value

def debug_print(*args, **kwargs):
    """Print only if verbose mode is enabled."""
    if VERBOSE:
        print(*args, **kwargs)

@lru_cache(maxsize=8)
def parse_zsim_output(file_path: str, use_h5: bool = False) -> List[Dict[str, Any]]:
    """Parse ZSim output file into a list of period dictionaries with caching."""
    if file_path in _PARSE_CACHE:
        return _PARSE_CACHE[file_path]
    
    start_time = time.time()
    
    if use_h5:
        if not file_path.endswith('.h5'):
            if not os.path.exists(file_path):
                debug_print(f"Warning: H5 file not found at {file_path}, falling back to text parsing")
                result = parse_zsim_text(file_path)
            else:
                result = parse_zsim_h5(file_path)
        else:
            result = parse_zsim_h5(file_path)
    else:
        result = parse_zsim_text(file_path)
    
    _PARSE_CACHE[file_path] = result
    
    end_time = time.time()
    debug_print(f"Parsed file in {end_time - start_time:.2f} seconds")
    
    return result


def parse_zsim_h5(file_path: str) -> List[Dict[str, Any]]:
    """Parse ZSim HDF5 output file into a list of period dictionaries."""
    result = []
    
    with h5py.File(file_path, 'r') as f:
        dset = f["stats"]["root"]
        
        # Convert each record to a dictionary
        for record_idx in range(len(dset)):
            period_dict = {}
            record = dset[record_idx]
            
            # Helper function to recursively convert structured numpy arrays to dict
            def convert_to_dict(arr):
                if isinstance(arr, np.void):  # Structured array record
                    return {name: convert_to_dict(arr[name]) for name in arr.dtype.names}
                elif isinstance(arr, np.ndarray):
                    if arr.dtype.names:  # Structured array
                        if len(arr.shape) > 1:  # Multi-dimensional structured array
                            return [convert_to_dict(x) for x in arr]
                        return {name: convert_to_dict(arr[name]) for name in arr.dtype.names}
                    else:  # Regular array
                        return arr.tolist()
                else:  # Regular value
                    if isinstance(arr, (np.integer, np.floating)):
                        return arr.item()
                    return arr
            
            # Convert the record to dictionary and wrap it in a root dictionary
            period_dict['root'] = convert_to_dict(record)
            result.append(period_dict)
    
    return result


def parse_zsim_text(file_path: str) -> List[Dict[str, Any]]:
    """Parse ZSim text output file into a list of period dictionaries."""
    # Try different encodings
    encodings = ['utf-8', 'latin-1', 'cp1252', 'ascii']
    content = None

    for encoding in encodings:
        try:
            with open(file_path, 'r', encoding=encoding) as f:
                content = f.read()
            break
        except UnicodeDecodeError:
            continue
    
    if content is None:
        # If all encodings fail, try binary mode and decode manually
        try:
            with open(file_path, 'rb') as f:
                content = f.read().decode('utf-8', errors='ignore')
        except Exception as e:
            raise RuntimeError(f"Could not read file {file_path} with any encoding: {str(e)}")

    # Rest of the parsing code remains the same
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
    
    return result


def find_cache_paths(data: List[Dict[str, Any]], base_path: str = "root.mem.mem-0") -> List[str]:
    """Find all cache paths under the given base path."""
    if not data:
        return []
    
    for period in data:
        if not period:
            continue
            
        debug_print(f"Examining period data: {period.keys()}")
        
        current = period['root']
        debug_print(f"Looking in structure: {current.keys()}")
        
        for cache_type in ['mem']:
            if cache_type in current:
                debug_print(f"Found standard mem: {cache_type}")
                cache_data = current[cache_type]
                if isinstance(cache_data, dict) and any(x in cache_data for x in ['loadHit', 'loadMiss', 'storeHit', 'storeMiss']):
                    return [cache_type]
        
        try:
            search_path = base_path
            paths_to_try = [
                search_path,
                search_path.replace('mem.mem-0', 'mem-0'),
                'root.mem.mem-0',
                'root.mem-0',
                'root.mem'
            ]
            
            debug_print(f"Trying paths: {paths_to_try}")
            
            for try_path in paths_to_try:
                try_current = period
                path_parts = try_path.split('.')
                
                valid_path = True
                for part in path_parts:
                    if part in try_current:
                        try_current = try_current[part]
                    else:
                        valid_path = False
                        break
                
                if valid_path and isinstance(try_current, dict):
                    for key, value in try_current.items():
                        if isinstance(value, dict):
                            if any(all(stat in value for stat in stats) for stats in [
                                ['loadHit', 'loadMiss'],
                                ['hits', 'misses'],
                                ['hGETS', 'hGETX'],
                                ['cleanEvict', 'dirtyEvict']
                            ]):
                                cache_path = f"{try_path}.{key}"
                                debug_print(f"Found cache at: {cache_path}")
                                return [cache_path]
                
        except (KeyError, AttributeError) as e:
            debug_print(f"Error while searching path: {e}")
            continue
    
    debug_print("No cache paths found in any period")
    return []


def find_core_paths(data: List[Dict[str, Any]], use_h5: bool = False) -> List[str]:
    """Find all core paths that have cycles, and instrs stats."""
    if not data:
        return []
    
    core_paths = set()  # Use set to avoid duplicates
    required_stats = {'cycles', 'instrs'}
    
    if use_h5:
        # Handle HDF5 format - data structure is flatter
        for period in data:
            if not period or 'root' not in period:
                continue
                
            root = period['root']
            # Scan all fields directly under root looking for core stats
            for key, value in root.items():
                # Check if this is a structured array or dict containing core stats
                if isinstance(value, (list, np.ndarray, dict)):
                    # For numpy arrays, check field names
                    if hasattr(value, 'dtype') and hasattr(value.dtype, 'names'):
                        if all(stat in value.dtype.names for stat in required_stats):
                            core_paths.add(f"root.{key}")
                    # For dictionaries, check keys directly
                    elif isinstance(value, dict):
                        if all(stat in value for stat in required_stats):
                            core_paths.add(f"root.{key}")
    else:
        def search_dict(d: Dict[str, Any], current_path: str):
            if not isinstance(d, dict):
                return
                
            for key, value in d.items():
                if not isinstance(value, dict):
                    continue
                    
                new_path = f"{current_path}.{key}" if current_path else key
                
                # Check if this is a core stats section
                if ('cycles' in value and 'instrs' in value) or \
                any(comment.strip() == '# Core stats' for comment in str(value).split('\n')):
                    core_paths.add(new_path)
                else:
                    search_dict(value, new_path)
        
        # Try each period until we find all core paths
        for period in data:
            if not period:  # Skip empty periods
                continue
                
            try:
                # Start search from root
                search_dict(period['root'], "root")
                if core_paths:  # If we found any paths, keep searching for more
                    continue
            except (KeyError, AttributeError):
                continue  # Try next period if any error occurs

    return sorted(list(core_paths))  # Return sorted list of unique paths


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


def get_value_by_path(data: Dict[str, Any], path: str) -> Union[int, float, str, List, None]:
    """Get a value from nested dictionary using a dot-notation path."""
    # Always ensure path starts with root
    if not path.startswith('root.'):
        path = 'root.' + path
        
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


def get_total_instructions(data: List[Dict[str, Any]], use_h5: bool = False) -> np.ndarray:
    """Calculate cumulative total instructions across all cores for each period using numpy."""
    if not data:
        return np.array([])
    
    # Find all core paths first
    core_paths = find_core_paths(data, use_h5=use_h5)
    if not core_paths:
        return np.array([])
    
    # Create paths for instruction counts
    instr_paths = [f"{core_path}.instrs" for core_path in core_paths]
    
    # Get instruction values for all cores
    extracted_values = get_multiple_values(data, instr_paths)
    
    # Convert to numpy array and handle None values
    instr_arrays = []
    
    if use_h5:
        # For H5 format, reorganize data from [periods][cores] to [cores][periods]
        num_periods = len(data)
        num_cores = len(extracted_values[instr_paths[0]][0]) if extracted_values[instr_paths[0]] else 0
        
        for core_idx in range(num_cores):
            # Collect all periods for this core
            core_values = []
            for period in range(num_periods):
                val = extracted_values[instr_paths[0]][period][core_idx]
                core_values.append(float(val) if val is not None else np.nan)
            instr_arrays.append(np.array(core_values))
    else:
        # Original handling for text format - already in [cores][periods] format
        for path in instr_paths:
            values = extracted_values[path]
            arr = np.array([float(v) if v is not None else np.nan for v in values])
            instr_arrays.append(arr)
    
    # Stack arrays and sum across cores
    if instr_arrays:
        stacked = np.vstack(instr_arrays)
        total_instrs = np.nansum(stacked, axis=0)
        return total_instrs
    
    return np.array([])


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
            # Filter out NaN values before summation
            window_hits = np.nansum(hits_diff[max(0, i - window_size + 1):i+1])
            window_misses = np.nansum(misses_diff[max(0, i - window_size + 1):i+1])
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
            window_load_hits = np.nansum(load_hits_diff[max(0, i - window_size + 1):i+1])
            window_load_misses = np.nansum(load_misses_diff[max(0, i - window_size + 1):i+1])
            window_store_hits = np.nansum(store_hits_diff[max(0, i - window_size + 1):i+1])
            window_store_misses = np.nansum(store_misses_diff[max(0, i - window_size + 1):i+1])
            
            total_hits = window_load_hits + window_store_hits
            total_accesses = total_hits + window_load_misses + window_store_misses
            
            if total_accesses > 0:
                rates[i] = total_hits / total_accesses
    
    return rates.tolist()


def calculate_ipc(file_path: str, base_path: str, window_size: int = 1, step: int = 1, use_h5: bool = False, rate_type: str = 'phase'):
    """Calculate Instructions Per Cycle (IPC) for each core and overall system."""
    start_time = time.time()
    
    # Parse file and find core paths
    data = parse_zsim_output(file_path, use_h5=use_h5)
    core_paths = find_core_paths(data, use_h5=use_h5)
    
    # Get total instructions if using instruction-based x-axis
    if rate_type == 'instr':
        total_instrs = get_total_instructions(data, use_h5=use_h5)
        if len(total_instrs) == 0:
            print("Warning: Could not get instruction counts, falling back to phase-based")
            rate_type = 'phase'
            x_values = np.arange(len(data))
        else:
            x_values = total_instrs / 1e9  # Convert to billions
    else:
        x_values = np.arange(len(data))
    
    if not core_paths:
        print("No core paths found")
        return {}, [], [], rate_type
    if VERBOSE:
        print(f"Found {len(core_paths)} cores:")
        for path in core_paths:
            print(f"  {path}")
        
    # Get all core stats in one pass
    paths_to_extract = []
    for core_path in core_paths:
        paths_to_extract.extend([
            f"{core_path}.cycles",
            f"{core_path}.instrs"
        ])
    
    extracted_values = get_multiple_values(data, paths_to_extract)
    num_periods = len(data)
    
    # First, get per-period sums across all cores
    total_metrics = {
        'cycles': np.zeros(num_periods),
        'instrs': np.zeros(num_periods)
    }
    
    if use_h5:
        # For H5 format, sum across cores for each period directly
        first_stat = next(iter(extracted_values.values()))
        num_cores = len(first_stat[0]) if isinstance(first_stat[0], (list, np.ndarray)) else 1
        
        # More efficient period-wise summation using numpy
        for metric in ['cycles', 'instrs']:
            metric_values = np.array([[float(extracted_values[f"{core_path}.{metric}"][p][i]) if extracted_values[f"{core_path}.{metric}"][p][i] is not None else 0 
                                     for i in range(num_cores)] 
                                    for p in range(num_periods)])
            total_metrics[metric] = np.sum(metric_values, axis=1)
    else:
        # For text format, reorganize and sum
        for core_path in core_paths:
            for metric in ['cycles', 'instrs']:
                values = extracted_values[f"{core_path}.{metric}"]
                total_metrics[metric] += np.array([float(v) if v is not None else 0 for v in values])
    
    # Calculate overall IPC using the totals
    total_cycles_diff = np.diff(total_metrics['cycles'], prepend=0)
    total_instrs_diff = np.diff(total_metrics['instrs'], prepend=0)
    
    overall_ipc = np.full(num_periods, np.nan)
    for i in range(0, num_periods, step):
        if i >= window_size - 1:
            window_slice = slice(max(0, i - window_size + 1), i + 1)
            window_cycles = np.sum(total_cycles_diff[window_slice])
            window_instrs = np.sum(total_instrs_diff[window_slice])
            
            total_cycles = window_cycles
            if total_cycles > 0:
                overall_ipc[i] = window_instrs / total_cycles
    
    # Calculate per-core IPC for reporting
    ipc_data = {}
    # Process each core's data
    for core_name, stats in extracted_values.items():
        core_ipc = np.full(num_periods, np.nan)
        for i in range(0, num_periods, step):
            if i >= window_size - 1:
                window_slice = slice(max(0, i - window_size + 1), i + 1)
                window_cycles = np.sum(total_cycles_diff[window_slice])
                window_instrs = np.sum(total_instrs_diff[window_slice])
                
                total_cycles = window_cycles
                if total_cycles > 0:
                    core_ipc[i] = window_instrs / total_cycles
        
        ipc_data[core_name] = core_ipc.tolist()
    
    end_time = time.time()
    print(f"IPC calculation completed in {end_time - start_time:.2f} seconds")
    
    return ipc_data, overall_ipc.tolist(), x_values.tolist(), rate_type


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
    elif stat_type.lower() == 'util':
        # For utilization stats
        return f"[{category}]-util-w{window_size}-s{step}-{cache_name}_{date}.png"
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


def plot_warning_graph(plot_path: str, output_filename: str, title: str, reason: str = None):
    """Create a plot with a warning message when no valid data is available."""
    setup_plot_style()
    fig = plt.figure(figsize=(12, 6))
    ax = fig.add_subplot(111)
    
    # Hide axes
    ax.set_xticks([])
    ax.set_yticks([])
    ax.spines['top'].set_visible(False)
    ax.spines['right'].set_visible(False)
    ax.spines['bottom'].set_visible(False)
    ax.spines['left'].set_visible(False)
    
    # Add warning text
    warning_text = reason if reason else 'No valid data available.\nTry reducing window size or step size.'
    ax.text(0.5, 0.5, warning_text,
            horizontalalignment='center',
            verticalalignment='center',
            fontsize=14,
            color='red',
            transform=ax.transAxes)
    
    ax.set_title(title)
    
    # Save plot
    if not os.path.exists(plot_path):
        os.makedirs(plot_path)
    plt.savefig(os.path.join(plot_path, output_filename), 
                bbox_inches='tight', pad_inches=0.1)
    plt.show()
    plt.close()


def check_valid_data_points(data: List[float], window_size: int, step: int) -> bool:
    """Check if there are any valid data points after applying window and step."""
    valid_indices = [i for i in range(0, len(data), step) if i >= window_size - 1]
    return any(not math.isnan(data[i]) for i in valid_indices)


def handle_single_point_plot(ax, x, y, label, color, line_style='-', alpha=0.7, linewidth=1.5):
    """Helper function to plot data that might have only one point."""
    if len(x) == 1:
        # For single point, use a visible marker
        ax.plot(x, y, line_style, color=color, label=label, alpha=alpha, linewidth=linewidth,
                marker='o', markersize=8, markerfacecolor=color, markeredgecolor='black')
    else:
        # Normal line plot for multiple points
        ax.plot(x, y, line_style, color=color, label=label, alpha=alpha, linewidth=linewidth)


def plot_cache_rate_trend(read_hit_rates: List[float], read_miss_rates: List[float], 
                         total_hit_rates: List[float],
                         zsim_dir: str, cache_name: str, window_size: int, step: int,
                         plot_path: str, x_values: np.ndarray, x_type: str,
                         ffi_points: List[int],  # Add ffi_points
                         warmup_instrs: int):  # Add warmup_instrs
    """Plot hit rate and miss rate trends using publication-quality style."""
    try:
        # Check if we have any valid data points after windowing
        has_data = any([
            check_valid_data_points(rates, window_size, step)
            for rates in [read_hit_rates, read_miss_rates, total_hit_rates]
        ])
        
        if not has_data:
            output_filename = get_output_name(zsim_dir, cache_name=cache_name, 
                                           stat_type='hit', window_size=window_size,
                                           step=step)
            title = f'Cache Performance: {cache_name}\n(Window Size: {window_size}, Step: {step})'
            plot_warning_graph(plot_path, output_filename, title)
            return

        # Update x_values length to match data length
        data_length = len(read_hit_rates)
        if len(x_values) > data_length:
            x_values = x_values[:data_length]
        elif len(x_values) < data_length:
            x_values = np.pad(x_values, (0, data_length - len(x_values)), 'edge')

        # Only plot points that meet the window size requirement
        valid_indices = [i for i in range(0, data_length, step) if i >= window_size - 1]
        if not valid_indices:
            reason = f"No valid data points with window={window_size} and step={step}.\nTry smaller values."
            plot_warning_graph(plot_path, output_filename, title, reason)
            return

        setup_plot_style()
        
        fig = plt.figure(figsize=(12, 6))
        ax = fig.add_subplot(111)
        
        # Get valid data points
        x_plot = x_values[valid_indices]
        
        # Plot each metric
        handle_single_point_plot(ax, x_plot, 
                               np.array(read_hit_rates)[valid_indices],
                               'Read Hit Rate', '#1f77b4')
        handle_single_point_plot(ax, x_plot, 
                               np.array(total_hit_rates)[valid_indices],
                               'Total Hit Rate', '#2ca02c')
        
        x_label = 'Billions of Instructions' if x_type == 'instr' else 'Period'
        ax.set_xlabel(x_label)
        ax.set_ylabel('Rate')
        ax.set_title(f'Cache Performance: {cache_name}\n(Window Size: {window_size}, Step: {step})')
        ax.set_ylim(-0.02, 1.02)
        ax.grid(True, linestyle=':', alpha=0.5, color='#cccccc')
        
        # Add text annotations for ffiPoints and warmupInstrs (use only first FFI point)
        text_str = f"Warmup Instructions: {warmup_instrs:.2e}\n"
        if ffi_points and len(ffi_points) > 0:
            text_str += f"FFI Point: {float(ffi_points[0]):.2e}"
        else:
            text_str += "FFI Point: None"
        
        # Place a text box in upper left in axes coords
        ax.text(0.02, 0.98, text_str, transform=ax.transAxes, fontsize=10,
                verticalalignment='top', bbox=dict(facecolor='white', alpha=0.7))
        
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
            'read_hit_rates': read_hit_rates,
            'read_miss_rates': read_miss_rates,
            'total_hit_rates': total_hit_rates,
            'x': x_values.tolist(),
            'x_label': x_label,
            'ffi_points': ffi_points,  # Save ffi_points
            'warmup_instrs': warmup_instrs  # Save warmup_instrs
        }, output_filename, plot_path)
        
        print(f"Plot saved to: {output_filename}")
        plt.show()
        plt.close()
        
    except Exception as e:
        print(f"Error creating plot: {e}")


def plot_ipc_trend(ipc_data, overall_ipc, zsim_dir, window_size, step, plot_path, x_values=None, x_type='phase',
                   ffi_points: List[int] = [],  # Add ffi_points
                   warmup_instrs: int = 0):  # Add warmup_instrs
    """Plot IPC trends with publication-quality style."""
    try:
        # Check if we have any valid data points after windowing
        has_data = any([
            check_valid_data_points(values, window_size, step)
            for values in ipc_data.values()
        ]) or check_valid_data_points(overall_ipc, window_size, step)
        
        if not has_data:
            output_filename = get_output_name(zsim_dir, cache_name="system", 
                                           stat_type='ipc', window_size=window_size,
                                           step=step)
            title = f'Instructions Per Cycle\n(Window Size: {window_size}, Step: {step})'
            plot_warning_graph(plot_path, output_filename, title)
            return

        # Update x_values if needed
        if x_values is None:
            x_values = np.arange(len(overall_ipc))
        else:
            x_values = np.array(x_values)  # Convert to numpy array
        
        data_length = len(overall_ipc)
        if len(x_values) > data_length:
            x_values = x_values[:data_length]
        elif len(x_values) < data_length:
            x_values = np.pad(x_values, (0, data_length - len(x_values)), 'edge')

        # Only plot points that meet the window size requirement
        valid_indices = np.array([i for i in range(0, data_length, step) if i >= window_size - 1])
        if len(valid_indices) == 0:
            reason = f"No valid data points with window={window_size} and step={step}.\nTry smaller values."
            plot_warning_graph(plot_path, output_filename, title, reason)
            return

        setup_plot_style()
        fig = plt.figure(figsize=(12, 6))
        ax = fig.add_subplot(111)
        
        # Create color palette for cores
        n_cores = len(ipc_data)
        colors = plt.cm.tab20(np.linspace(0, 1, n_cores))
        
        # Get valid data points
        x_plot = x_values[valid_indices]
        
        # Plot each core's IPC
        for (core_name, ipc_values), color in zip(ipc_data.items(), colors):
            ipc_array = np.array(ipc_values)  # Convert to numpy array
            handle_single_point_plot(ax, x_plot, 
                                   ipc_array[valid_indices],
                                   core_name, color)
        
        # Plot overall IPC with thick black line
        overall_array = np.array(overall_ipc)  # Convert to numpy array
        handle_single_point_plot(ax, x_plot, 
                               overall_array[valid_indices],
                               'Overall', 'black', 
                               linewidth=2)
        
        x_label = 'Billions of Instructions' if x_type == 'instr' else 'Period'
        ax.set_xlabel(x_label)
        ax.set_ylabel('Instructions Per Cycle (IPC)')
        ax.set_title(f'Instructions Per Cycle\n(Window Size: {window_size}, Step: {step})')
        ax.grid(True, linestyle=':', alpha=0.5, color='#cccccc')
        
        # Add text annotations for ffiPoints and warmupInstrs (use only first FFI point)
        text_str = f"Warmup Instructions: {warmup_instrs:.2e}\n"
        if ffi_points and len(ffi_points) > 0:
            text_str += f"FFI Point: {float(ffi_points[0]):.2e}"
        else:
            text_str += "FFI Point: None"
        
        # Place a text box in upper left in axes coords
        ax.text(0.02, 0.98, text_str, transform=ax.transAxes, fontsize=10,
                verticalalignment='top', bbox=dict(facecolor='white', alpha=0.7))

        box = ax.get_position()
        ax.set_position([box.x0, box.y0, box.width * 0.85, box.height])
        ax.legend(loc='center left', bbox_to_anchor=(1.05, 0.5),
                 frameon=True, edgecolor='black', fancybox=False,
                 ncol=1 if n_cores <= 8 else 2)
        
        # Save plot and data
        if not os.path.exists(plot_path):
            os.makedirs(plot_path)
        output_filename = get_output_name(zsim_dir, cache_name="system", stat_type='ipc', 
                                        window_size=window_size, step=step)
        plt.savefig(os.path.join(plot_path, output_filename), bbox_inches='tight', pad_inches=0.1)
        
        save_plot_data({
            'ipc_data': ipc_data,
            'overall_ipc': overall_ipc,
            'x': x_values.tolist(),
            'x_label': x_label,
            'ffi_points': ffi_points,  # Save ffi_points
            'warmup_instrs': warmup_instrs  # Save warmup_instrs
        }, output_filename, plot_path)
        
        print(f"Plot saved to: {output_filename}")
        plt.show()
        plt.close()
        
    except Exception as e:
        print(f"Error creating IPC plot: {e}")


def calculate_cache_util_trend(values, total, window_size, rate_type, step):
    if len(values) != len(total):
        raise ValueError("Values and total lists must have the same length")

    # Convert to numpy arrays, handling None values
    values_array = np.array([float(v) if v is not None else np.nan for v in values])
    total_array = np.array([float(t) if t is not None else np.nan for t in total])

    # Calculate differences if tracking changes over time
    values_diff = np.diff(values_array, prepend=0)
    
    # Initialize result array
    rates = np.full(len(values), np.nan)
    
    # Calculate rates for each window with the given step size
    for i in range(0, len(values), step):
        if i >= window_size - 1:
            # Get window slice
            window_slice = slice(max(0, i - window_size + 1), i + 1)
            
            # Calculate based on rate type
            if rate_type in ['cache_util', 'cache_reaccess']:
                if not np.isnan(total_array[i]) and total_array[i] > 0:
                    rates[i] = (values_array[i] / total_array[i])
            
            elif rate_type in ['ext_mem_util', 'ext_pages_util']:
                if not np.isnan(total_array[i]) and total_array[i] > 0:
                    rates[i] = (values_array[i] / total_array[i])

    return rates.tolist()


def plot_cache_util_trend(cache_util_rates: List[float], 
                         cache_reaccess_rates: List[float],
                         ext_mem_rates: List[float],
                         ext_pages_rates: List[float],
                         zsim_dir: str, cache_name: str, 
                         window_size: int, step: int,
                         plot_path: str, 
                         x_values: np.ndarray, 
                         x_type: str,
                         ffi_points: List[int],  # Add ffi_points
                         warmup_instrs: int):  # Add warmup_instrs
    """Plot all utilization trends in a single figure using publication-quality style."""
    try:
        # Check if we have any valid data points after windowing
        has_data = any([
            check_valid_data_points(rates, window_size, step)
            for rates in [cache_util_rates, cache_reaccess_rates, ext_mem_rates, ext_pages_rates]
        ])
        
        if not has_data:
            output_filename = get_output_name(zsim_dir, cache_name=cache_name, 
                                           stat_type='util', window_size=window_size,
                                           step=step)
            title = f'Memory System Utilization: {cache_name}\n(Window Size: {window_size}, Step: {step})'
            plot_warning_graph(plot_path, output_filename, title)
            return

        # Update x_values length to match data length
        data_length = len(cache_util_rates)
        if len(x_values) > data_length:
            x_values = x_values[:data_length]
        elif len(x_values) < data_length:
            x_values = np.pad(x_values, (0, data_length - len(x_values)), 'edge')

        # Only plot points that meet the window size requirement
        valid_indices = [i for i in range(0, data_length, step) if i >= window_size - 1]
        if not valid_indices:
            reason = f"No valid data points with window={window_size} and step={step}.\nTry smaller values."
            plot_warning_graph(plot_path, output_filename, title, reason)
            return

        setup_plot_style()
        
        # Create single figure
        fig = plt.figure(figsize=(12, 6))
        ax = fig.add_subplot(111)
        
        # Get valid data points
        x_plot = x_values[valid_indices]
        
        # Force y-axis to go from 0 to 100 to ensure all util data is shown
        ax.set_ylim(0, 100)
        
        # Plot all metrics with different colors and line styles - ensure visibility
        if any(not math.isnan(rate) for rate in np.array(cache_util_rates)[valid_indices]):
            handle_single_point_plot(ax, x_plot, 
                                   np.array(cache_util_rates)[valid_indices]*100,
                                   'Cache Utilization', '#1f77b4', '-')
        
        if any(not math.isnan(rate) for rate in np.array(cache_reaccess_rates)[valid_indices]):
            handle_single_point_plot(ax, x_plot, 
                                   np.array(cache_reaccess_rates)[valid_indices]*100,
                                   'Cache Re-access', '#2ca02c', '--')
        
        if any(not math.isnan(rate) for rate in np.array(ext_mem_rates)[valid_indices]):
            handle_single_point_plot(ax, x_plot, 
                                   np.array(ext_mem_rates)[valid_indices]*100,
                                   'Ext Memory Utilization', '#ff7f0e', ':', linewidth=2.0)
        
        if any(not math.isnan(rate) for rate in np.array(ext_pages_rates)[valid_indices]):
            handle_single_point_plot(ax, x_plot, 
                                   np.array(ext_pages_rates)[valid_indices]*100,
                                   'Ext Pages Utilization', '#d62728', '-.', linewidth=2.0)
        
        # Configure axis
        x_label = 'Billions of Instructions' if x_type == 'instr' else 'Period'
        ax.set_xlabel(x_label)
        ax.set_ylabel('Utilization (%)')
        # Don't use auto-scaling for y-axis to ensure all data is visible
        ax.set_ylim(-0.02, 100.02)
        ax.grid(True, linestyle=':', alpha=0.5, color='#cccccc')
        
        # Add text annotations for ffiPoints and warmupInstrs (use only first FFI point)
        text_str = f"Warmup Instructions: {warmup_instrs:.2e}\n"
        if ffi_points and len(ffi_points) > 0:
            text_str += f"FFI Point: {float(ffi_points[0]):.2e}"
        else:
            text_str += "FFI Point: None"
        
        # Place a text box in upper left in axes coords
        ax.text(0.02, 0.98, text_str, transform=ax.transAxes, fontsize=10,
                verticalalignment='top', bbox=dict(facecolor='white', alpha=0.7))
        
        # Place legend outside plot
        box = ax.get_position()
        ax.set_position([box.x0, box.y0, box.width * 0.85, box.height])
        ax.legend(loc='center left', bbox_to_anchor=(1.05, 0.5),
                 frameon=True, edgecolor='black', fancybox=False)
        
        ax.set_title(f'Memory System Utilization: {cache_name}\n(Window Size: {window_size}, Step: {step})')
        
        # Save plot
        if not os.path.exists(plot_path):
            os.makedirs(plot_path)
            
        output_filename = get_output_name(zsim_dir, cache_name=cache_name, 
                                        stat_type='util', window_size=window_size,
                                        step=step)
        
        plt.savefig(os.path.join(plot_path, output_filename), 
                    bbox_inches='tight', pad_inches=0.1)
        
        # Save the data points
        save_plot_data({
            'cache_util_rates': cache_util_rates,
            'cache_reaccess_rates': cache_reaccess_rates,
            'ext_mem_rates': ext_mem_rates,
            'ext_pages_rates': ext_pages_rates,
            'x': x_values.tolist(),
            'x_label': x_label,
            'ffi_points': ffi_points,  # Save ffi_points
            'warmup_instrs': warmup_instrs  # Save warmup_instrs
        }, output_filename, plot_path)
        
        print(f"Plot saved to: {output_filename}")
        plt.show()
        plt.close()
        
    except Exception as e:
        print(f"Error creating utilization plot: {e}")


def combine_plots(plots_dir: str):
    """Combine all plots in a directory into comparison graphs with consistent category colors."""
    # Create plots directory if it doesn't exist
    os.makedirs(plots_dir, exist_ok=True)
    distinct_colors = [
        '#1f77b4',  # Blue
        '#ff7f0e',  # Orange
        '#2ca02c',  # Green
        '#d62728',  # Red
        '#9467bd',  # Purple
        '#8c564b',  # Brown
        '#e377c2',  # Pink
        '#7f7f7f',  # Gray
        '#bcbd22',  # Olive
        '#17becf',  # Cyan
        '#ff1493',  # Deep Pink
        '#00ced1',  # Dark Turquoise
        '#ff4500',  # Orange Red
        '#9acd32',  # Yellow Green
        '#4682b4',  # Steel Blue
    ]
    # Get all data files
    data_files = [f for f in os.listdir(plots_dir) if f.endswith('.npz')]
    
    # Group plots by type and window size
    hit_plots = {}  # window_size -> {category -> (cache_name, date, data, step, is_instr_based, x_values)}
    ipc_plots = {}  # window_size -> {category -> (date, data, step, x_values)}
    util_plots = {}  # window_size -> {category -> (cache_name, date, data, step, is_instr_based, x_values)}
    
    for data_file in data_files:
        # Parse filename: [category]-type-w{window}-s{step}-{cache}_{date}.npz
        match = re.match(r'\[(.*?)\]-(hit|ipc|util)-w(\d+)-s(\d+)-(.+?)_(\d{8}-\d{6})\.npz', data_file)
        if not match:
            continue
            
        category, plot_type, window_size, step_size, cache_name, date = match.groups()
        window_size = int(window_size)
        step_size = int(step_size)
        
        try:
            # Load the data
            data = dict(np.load(os.path.join(plots_dir, data_file), allow_pickle=True))
            
            # Check if this is instruction-based data
            x_label = str(data.get('x_label', 'Period'))
            is_instr_based = 'Instruction' in x_label
            
            # Make sure x values are available
            x_values = data.get('x', [])
            if len(x_values) == 0:
                continue
                
            if plot_type == 'hit':
                if window_size not in hit_plots:
                    hit_plots[window_size] = {}
                hit_plots[window_size][category] = (cache_name, date, data, step_size, is_instr_based, x_values)
            elif plot_type == 'ipc':
                if window_size not in ipc_plots:
                    ipc_plots[window_size] = {}
                ipc_plots[window_size][category] = (date, data, step_size, x_values)
            elif plot_type == 'util':
                if window_size not in util_plots:
                    util_plots[window_size] = {}
                util_plots[window_size][category] = (cache_name, date, data, step_size, is_instr_based, x_values)
        except Exception as e:
            print(f"Error loading {data_file}: {e}")
            continue
    
    # Extract benchmark name from category
    def get_benchmark(category):
        parts = category.split('_')
        if len(parts) <= 1:
            return category
        return '_'.join(parts[1:])
    
    # Create consistent category-to-color mapping
    all_categories = set()
    for window_size in hit_plots:
        all_categories.update(hit_plots[window_size].keys())
    for window_size in ipc_plots:
        all_categories.update(ipc_plots[window_size].keys())
    for window_size in util_plots:
        all_categories.update(util_plots[window_size].keys())
    # Group categories by base name (before first underscore)
    base_categories= set([category.split('_')[0] for category in all_categories])
    base_category_colors = dict([(base_category, distinct_colors[i % len(distinct_colors)]) for i, base_category in enumerate(sorted(base_categories))])
    category_colors = dict([(category, distinct_colors[i % len(distinct_colors)]) for i, category in  enumerate(sorted(all_categories))])

    if VERBOSE:
        print("\nCategory-to-Color Mappings:")
        for category, color in sorted(category_colors.items()):  # Sort for readable output
            print(f"  {category}: {color}")
    # Process each window size
    for window_size in sorted(set(list(hit_plots.keys()) + list(ipc_plots.keys()) + list(util_plots.keys()))):
        # Plot hit rates if we have any
        if window_size in hit_plots and hit_plots[window_size]:
            first_category = next(iter(hit_plots[window_size]))
            _, _, _, step_size, is_instr_based, _ = hit_plots[window_size][first_category]
            
            # Set x-axis label based on data type
            x_label = 'Billions of Instructions' if is_instr_based else 'Period'
            
            # Read Hit Rate combined plot
            setup_plot_style()
            fig = plt.figure(figsize=(12, 6))
            ax = fig.add_subplot(111)
            
            # Add FFI points and warmup instructions info from first available data entry
            any_data_has_ffi = False
            for category, (cache_name, date, data, _, _, x_values) in hit_plots[window_size].items():
                if 'ffi_points' in data and 'warmup_instrs' in data:
                    ffi_points = data['ffi_points']
                    warmup_instrs = data['warmup_instrs']
                    
                    # Create text string with first FFI point only in scientific notation
                    text_str = f"Warmup Instructions: {float(warmup_instrs):.2e}\n"
                    if isinstance(ffi_points, (list, tuple, np.ndarray)) and len(ffi_points) > 0:
                        text_str += f"FFI Point: {float(ffi_points[0]):.2e}"
                    else:
                        text_str += "FFI Point: None"
                    
                    # Place text box in upper left
                    ax.text(0.02, 0.98, text_str, transform=ax.transAxes, fontsize=10,
                            verticalalignment='top', bbox=dict(facecolor='white', alpha=0.7))
                    any_data_has_ffi = True
                    break
            
            for category, (cache_name, date, data, _, _, x_values) in hit_plots[window_size].items():
                hit_rates = data['read_hit_rates'] if 'read_hit_rates' in data else data['hit_rates']
                # Convert to numpy arrays
                x_plot = np.array(x_values)
                y_plot = np.array(hit_rates)
                
                # Get valid points (non-NaN)
                valid_mask = ~np.isnan(y_plot)
                if np.any(valid_mask):  # If we have any valid points
                    if np.sum(valid_mask) == 1:  # Single point
                        # Find the index of the valid point
                        valid_idx = np.where(valid_mask)[0][0]
                        ax.plot(x_plot[valid_idx:valid_idx+1], y_plot[valid_idx:valid_idx+1], 
                               'o', label=category, color=category_colors[category],
                               markersize=8, markerfacecolor=category_colors[category],
                               markeredgecolor='black')
                    else:  # Multiple points
                        ax.plot(x_plot[::step_size], y_plot[::step_size], '-', 
                               label=category, color=category_colors[category],
                               alpha=0.7, linewidth=1.5)
            
            ax.set_xlabel(x_label)
            ax.set_ylabel('Read Hit Rate')
            ax.set_title(f'Cache Read Hit Rate Comparison\n(Window Size: {window_size}, Step: {step_size})')
            ax.set_ylim(-0.02, 1.02)
            ax.grid(True, linestyle=':', alpha=0.5, color='#cccccc')
            
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
            
            # Add FFI points and warmup instructions info
            if any_data_has_ffi:
                ax.text(0.02, 0.98, text_str, transform=ax.transAxes, fontsize=10,
                        verticalalignment='top', bbox=dict(facecolor='white', alpha=0.7))
            
            for category, (cache_name, date, data, _, _, x_values) in hit_plots[window_size].items():
                if 'total_hit_rates' in data:
                    total_hit_rates = data['total_hit_rates']
                    x_plot = np.array(x_values)
                    y_plot = np.array(total_hit_rates)
                    
                    # Get valid points (non-NaN)
                    valid_mask = ~np.isnan(y_plot)
                    if np.any(valid_mask):  # If we have any valid points
                        if np.sum(valid_mask) == 1:  # Single point
                            valid_idx = np.where(valid_mask)[0][0]
                            ax.plot(x_plot[valid_idx:valid_idx+1], y_plot[valid_idx:valid_idx+1], 
                                   'o', label=category, color=category_colors[category],
                                   markersize=8, markerfacecolor=category_colors[category],
                                   markeredgecolor='black')
                        else:  # Multiple points
                            ax.plot(x_plot[::step_size], y_plot[::step_size], '-', 
                                   label=category, color=category_colors[category],
                                   alpha=0.7, linewidth=1.5)
            
            ax.set_xlabel(x_label)
            ax.set_ylabel('Total Hit Rate')
            ax.set_title(f'Cache Total Hit Rate Comparison\n(Window Size: {window_size}, Step: {step_size})')
            ax.set_ylim(-0.02, 1.02)
            ax.grid(True, linestyle=':', alpha=0.5, color='#cccccc')
            
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
                    continue
                
                setup_plot_style()
                fig = plt.figure(figsize=(12, 6))
                ax = fig.add_subplot(111)
                
                # Add FFI points and warmup instructions info
                if any_data_has_ffi:
                    ax.text(0.02, 0.98, text_str, transform=ax.transAxes, fontsize=10,
                            verticalalignment='top', bbox=dict(facecolor='white', alpha=0.7))
                
                for category in sorted(categories):
                    cache_name, date, data, _, _, x_values = hit_plots[window_size][category]
                    hit_rates = data['read_hit_rates'] if 'read_hit_rates' in data else data['hit_rates']
                    x_plot = np.array(x_values)
                    y_plot = np.array(hit_rates)
                    
                    # Get valid points (non-NaN)
                    valid_mask = ~np.isnan(y_plot)
                    if np.any(valid_mask):  # If we have any valid points
                        if np.sum(valid_mask) == 1:  # Single point
                            valid_idx = np.where(valid_mask)[0][0]
                            ax.plot(x_plot[valid_idx:valid_idx+1], y_plot[valid_idx:valid_idx+1], 
                                   'o', label=category, color=base_category_colors[category.split('_')[0]],
                                   markersize=8, markerfacecolor=base_category_colors[category.split('_')[0]],
                                   markeredgecolor='black')
                        else:  # Multiple points
                            ax.plot(x_plot[::step_size], y_plot[::step_size], '-', 
                                   label=category, color=base_category_colors[category.split('_')[0]],
                                   alpha=0.7, linewidth=1.5)
                
                ax.set_xlabel(x_label)
                ax.set_ylabel('Read Hit Rate')
                ax.set_title(f'Cache Read Hit Rate Comparison for {benchmark}\n(Window Size: {window_size}, Step: {step_size})')
                ax.set_ylim(-0.02, 1.02)
                ax.grid(True, linestyle=':', alpha=0.5, color='#cccccc')
                
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
                
                # Add FFI points and warmup instructions info
                if any_data_has_ffi:
                    ax.text(0.02, 0.98, text_str, transform=ax.transAxes, fontsize=10,
                            verticalalignment='top', bbox=dict(facecolor='white', alpha=0.7))
                
                has_data = False
                for category in sorted(categories):
                    cache_name, date, data, _, _, x_values = hit_plots[window_size][category]
                    if 'total_hit_rates' in data:
                        total_hit_rates = data['total_hit_rates']
                        x_plot = np.array(x_values)
                        y_plot = np.array(total_hit_rates)
                        
                        # Get valid points (non-NaN)
                        valid_mask = ~np.isnan(y_plot)
                        if np.any(valid_mask):  # If we have any valid points
                            if np.sum(valid_mask) == 1:  # Single point
                                valid_idx = np.where(valid_mask)[0][0]
                                ax.plot(x_plot[valid_idx:valid_idx+1], y_plot[valid_idx:valid_idx+1], 
                                       'o', label=category, color=base_category_colors[category.split('_')[0]],
                                       markersize=8, markerfacecolor=base_category_colors[category.split('_')[0]],
                                       markeredgecolor='black')
                            else:  # Multiple points
                                ax.plot(x_plot[::step_size], y_plot[::step_size], '-', 
                                       label=category, color=base_category_colors[category.split('_')[0]],
                                       alpha=0.7, linewidth=1.5)
                        has_data = True
                
                if has_data:
                    ax.set_xlabel(x_label)
                    ax.set_ylabel('Total Hit Rate')
                    ax.set_title(f'Cache Total Hit Rate Comparison for {benchmark}\n(Window Size: {window_size}, Step: {step_size})')
                    ax.set_ylim(-0.02, 1.02)
                    ax.grid(True, linestyle=':', alpha=0.5, color='#cccccc')
                    
                    box = ax.get_position()
                    ax.set_position([box.x0, box.y0, box.width * 0.85, box.height])
                    ax.legend(loc='center left', bbox_to_anchor=(1.05, 0.5),
                             frameon=True, edgecolor='black', fancybox=False)
                    
                    plt.savefig(os.path.join(plots_dir, f'benchmark_{benchmark}_thit_w{window_size}_s{step_size}.png'),
                               bbox_inches='tight', pad_inches=0.1)
                    plt.close()
        
        # Plot IPC if we have any
        if window_size in ipc_plots and ipc_plots[window_size]:
            first_category = next(iter(ipc_plots[window_size]))
            _, _, step_size, _ = ipc_plots[window_size][first_category]
            
            setup_plot_style()
            fig = plt.figure(figsize=(12, 6))
            ax = fig.add_subplot(111)
            
            # Add FFI points and warmup instructions info from first available data entry
            any_data_has_ffi = False
            for category, (date, data, _, x_values) in ipc_plots[window_size].items():
                if 'ffi_points' in data and 'warmup_instrs' in data:
                    ffi_points = data['ffi_points']
                    warmup_instrs = data['warmup_instrs']
                    
                    # Create text string with first FFI point only in scientific notation
                    text_str = f"Warmup Instructions: {float(warmup_instrs):.2e}\n"
                    if isinstance(ffi_points, (list, tuple, np.ndarray)) and len(ffi_points) > 0:
                        text_str += f"FFI Point: {float(ffi_points[0]):.2e}"
                    else:
                        text_str += "FFI Point: None"
                    
                    # Place text box in upper left
                    ax.text(0.02, 0.98, text_str, transform=ax.transAxes, fontsize=10,
                            verticalalignment='top', bbox=dict(facecolor='white', alpha=0.7))
                    any_data_has_ffi = True
                    break
            
            # Find the range of x values across all data
            x_min, x_max = float('inf'), float('-inf')
            max_len = 0
            for _, (_, data, _, x_values) in ipc_plots[window_size].items():
                if len(x_values) > 0:
                    x_min = min(x_min, x_values[0])
                    x_max = max(x_max, x_values[-1])
                max_len = max(max_len, len(data['overall_ipc']))
            
            # Create a consistent x-axis for all plots
            x_base = np.linspace(x_min, x_max, max_len)
            
            for category, (date, data, _, x_values) in ipc_plots[window_size].items():
                ipc_values = np.array(data['overall_ipc'])
                x_plot = np.array(x_values)
                
                # Get valid points (non-NaN)
                valid_mask = ~np.isnan(ipc_values)
                if np.any(valid_mask):  # If we have any valid points
                    if np.sum(valid_mask) == 1:  # Single point
                        valid_idx = np.where(valid_mask)[0][0]
                        ax.plot(x_plot[valid_idx:valid_idx+1], ipc_values[valid_idx:valid_idx+1], 
                               'o', label=category, color=category_colors[category],
                               markersize=8, markerfacecolor=category_colors[category],
                               markeredgecolor='black')
                    else:  # Multiple points
                        ax.plot(x_plot[::step_size], ipc_values[::step_size], '-', 
                               label=category, color=category_colors[category],
                               alpha=0.7, linewidth=1.5)
            
            ax.set_xlabel('Billions of Instructions' if 'Instruction' in str(data.get('x_label', 'Period')) else 'Period')
            ax.set_ylabel('Instructions Per Cycle (IPC)')
            ax.set_title(f'IPC Comparison\n(Window Size: {window_size}, Step: {step_size})')
            ax.grid(True, linestyle=':', alpha=0.5, color='#cccccc')
            
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
                    continue
                
                setup_plot_style()
                fig = plt.figure(figsize=(12, 6))
                ax = fig.add_subplot(111)
                
                # Add FFI points and warmup instructions info
                if any_data_has_ffi:
                    ax.text(0.02, 0.98, text_str, transform=ax.transAxes, fontsize=10,
                            verticalalignment='top', bbox=dict(facecolor='white', alpha=0.7))
                
                for category in sorted(categories):
                    date, data, _, x_values = ipc_plots[window_size][category]
                    ipc_values = np.array(data['overall_ipc'])
                    x_plot = np.array(x_values)
                    
                    # Get valid points (non-NaN)
                    valid_mask = ~np.isnan(ipc_values)
                    if np.any(valid_mask):  # If we have any valid points
                        if np.sum(valid_mask) == 1:  # Single point
                            valid_idx = np.where(valid_mask)[0][0]
                            ax.plot(x_plot[valid_idx:valid_idx+1], ipc_values[valid_idx:valid_idx+1], 
                                   'o', label=category, color=base_category_colors[category.split('_')[0]],
                                   markersize=8, markerfacecolor=base_category_colors[category.split('_')[0]],
                                   markeredgecolor='black')
                        else:  # Multiple points
                            ax.plot(x_plot[::step_size], ipc_values[::step_size], '-', 
                                   label=category, color=base_category_colors[category.split('_')[0]],
                                   alpha=0.7, linewidth=1.5)
                
                ax.set_xlabel('Billions of Instructions' if 'Instruction' in str(data.get('x_label', 'Period')) else 'Period')
                ax.set_ylabel('Instructions Per Cycle (IPC)')
                ax.set_title(f'IPC Comparison for {benchmark}\n(Window Size: {window_size}, Step: {step_size})')
                ax.grid(True, linestyle=':', alpha=0.5, color='#cccccc')
                
                box = ax.get_position()
                ax.set_position([box.x0, box.y0, box.width * 0.85, box.height])
                ax.legend(loc='center left', bbox_to_anchor=(1.05, 0.5),
                         frameon=True, edgecolor='black', fancybox=False)
                
                plt.savefig(os.path.join(plots_dir, f'benchmark_{benchmark}_ipc_w{window_size}_s{step_size}.png'),
                           bbox_inches='tight', pad_inches=0.1)
                plt.close()
        
        # Plot utilization rates if we have any
        if window_size in util_plots and util_plots[window_size]:
            first_category = next(iter(util_plots[window_size]))
            _, _, _, step_size, is_instr_based, _ = util_plots[window_size][first_category]
            
            x_label = 'Billions of Instructions' if is_instr_based else 'Period'
            
            # Combined utilization plot
            setup_plot_style()
            fig = plt.figure(figsize=(12, 6))
            ax = fig.add_subplot(111)
            
            # Add FFI points and warmup instructions info from first available data entry
            any_data_has_ffi = False
            for category, (cache_name, date, data, _, _, x_values) in util_plots[window_size].items():
                if 'ffi_points' in data and 'warmup_instrs' in data:
                    ffi_points = data['ffi_points']
                    warmup_instrs = data['warmup_instrs']
                    
                    # Create text string with first FFI point only in scientific notation
                    text_str = f"Warmup Instructions: {float(warmup_instrs):.2e}\n"
                    if isinstance(ffi_points, (list, tuple, np.ndarray)) and len(ffi_points) > 0:
                        text_str += f"FFI Point: {float(ffi_points[0]):.2e}"
                    else:
                        text_str += "FFI Point: None"
                    
                    # Place text box in upper left
                    ax.text(0.02, 0.98, text_str, transform=ax.transAxes, fontsize=10,
                            verticalalignment='top', bbox=dict(facecolor='white', alpha=0.7))
                    any_data_has_ffi = True
                    break
            
            # Plot all categories' utilization data
            for category, (cache_name, date, data, _, _, x_values) in util_plots[window_size].items():
                x_plot = np.array(x_values)
                
                # Plot cache utilization with solid line
                if 'cache_util_rates' in data:
                    cache_util = np.array(data['cache_util_rates']) * 100
                    valid_mask = ~np.isnan(cache_util)
                    if np.any(valid_mask):  # If we have any valid points
                        if np.sum(valid_mask) == 1:  # Single point
                            valid_idx = np.where(valid_mask)[0][0]
                            ax.plot(x_plot[valid_idx:valid_idx+1], cache_util[valid_idx:valid_idx+1], 
                                   'o', label=f'{category} Cache', color=category_colors[category],
                                   markersize=8, markerfacecolor=category_colors[category],
                                   markeredgecolor='black')
                        else:  # Multiple points
                            ax.plot(x_plot[::step_size], cache_util[::step_size], '-', 
                                   label=f'{category} Cache', color=category_colors[category],
                                   alpha=0.7, linewidth=1.5)
                
                # Plot ext memory utilization with dashed line
                if 'ext_mem_rates' in data:
                    ext_mem = np.array(data['ext_mem_rates']) * 100
                    valid_mask = ~np.isnan(ext_mem)
                    if np.any(valid_mask):  # If we have any valid points
                        if np.sum(valid_mask) == 1:  # Single point
                            valid_idx = np.where(valid_mask)[0][0]
                            ax.plot(x_plot[valid_idx:valid_idx+1], ext_mem[valid_idx:valid_idx+1], 
                                   'o', label=f'{category} ExtMem', color=category_colors[category],
                                   markersize=8, markerfacecolor=category_colors[category],
                                   markeredgecolor='black', linestyle='--')
                        else:  # Multiple points
                            ax.plot(x_plot[::step_size], ext_mem[::step_size], '--', 
                                   label=f'{category} ExtMem', color=category_colors[category],
                                   alpha=0.8, linewidth=2.0)
            
            ax.set_xlabel(x_label)
            ax.set_ylabel('Utilization (%)')
            ax.set_title(f'Memory System Utilization Comparison\n(Window Size: {window_size}, Step: {step_size})')
            ax.set_ylim(-0.02, 100.02)
            ax.grid(True, linestyle=':', alpha=0.5, color='#cccccc')
            
            box = ax.get_position()
            ax.set_position([box.x0, box.y0, box.width * 0.85, box.height])
            ax.legend(loc='center left', bbox_to_anchor=(1.05, 0.5),
                     frameon=True, edgecolor='black', fancybox=False)
            
            plt.savefig(os.path.join(plots_dir, f'combined_util_w{window_size}_s{step_size}.png'),
                       bbox_inches='tight', pad_inches=0.1)
            plt.close()
            
            # Create benchmark-specific plots
            benchmark_groups = {}
            for category in util_plots[window_size]:
                benchmark = get_benchmark(category)
                if benchmark not in benchmark_groups:
                    benchmark_groups[benchmark] = []
                benchmark_groups[benchmark].append(category)
            
            for benchmark, categories in benchmark_groups.items():
                if len(categories) <= 1:
                    continue
                
                setup_plot_style()
                fig = plt.figure(figsize=(12, 6))
                ax = fig.add_subplot(111)
                
                # Add FFI points and warmup instructions info
                if any_data_has_ffi:
                    ax.text(0.02, 0.98, text_str, transform=ax.transAxes, fontsize=10,
                            verticalalignment='top', bbox=dict(facecolor='white', alpha=0.7))
                
                for category in sorted(categories):
                    cache_name, date, data, _, _, x_values = util_plots[window_size][category]
                    x_plot = np.array(x_values)
                    if 'cache_util_rates' in data and 'ext_mem_rates' in data:
                        cache_util = np.array(data['cache_util_rates']) * 100
                        ext_mem = np.array(data['ext_mem_rates']) * 100
                        color = base_category_colors[category.split('_')[0]]
                        
                        # Get valid points (non-NaN)
                        valid_cache_mask = ~np.isnan(cache_util)
                        valid_ext_mask = ~np.isnan(ext_mem)
                        
                        if np.any(valid_cache_mask):  # Cache utilization
                            if np.sum(valid_cache_mask) == 1:  # Single point
                                valid_idx = np.where(valid_cache_mask)[0][0]
                                ax.plot(x_plot[valid_idx:valid_idx+1], cache_util[valid_idx:valid_idx+1], 
                                       'o', label=f'{category} Cache', color=color,
                                       markersize=8, markerfacecolor=color,
                                       markeredgecolor='black')
                            else:  # Multiple points
                                ax.plot(x_plot[::step_size], cache_util[::step_size], '-', 
                                       label=f'{category} Cache', color=color,
                                       alpha=0.7, linewidth=1.5)
                        
                        if np.any(valid_ext_mask):  # Ext memory utilization
                            if np.sum(valid_ext_mask) == 1:  # Single point
                                valid_idx = np.where(valid_ext_mask)[0][0]
                                ax.plot(x_plot[valid_idx:valid_idx+1], ext_mem[valid_idx:valid_idx+1], 
                                       'o', label=f'{category} ExtMem', color=color,
                                       markersize=8, markerfacecolor=color,
                                       markeredgecolor='black', linestyle='--')
                            else:  # Multiple points
                                ax.plot(x_plot[::step_size], ext_mem[::step_size], '--', 
                                       label=f'{category} ExtMem', color=color,
                                       alpha=0.8, linewidth=2.0)
                
                ax.set_xlabel(x_label)
                ax.set_ylabel('Utilization (%)')
                ax.set_title(f'Memory System Utilization for {benchmark}\n(Window Size: {window_size}, Step: {step_size})')
                ax.set_ylim(-0.02, 100.02)
                ax.grid(True, linestyle=':', alpha=0.5, color='#cccccc')
                
                box = ax.get_position()
                ax.set_position([box.x0, box.y0, box.width * 0.85, box.height])
                ax.legend(loc='center left', bbox_to_anchor=(1.05, 0.5),
                         frameon=True, edgecolor='black', fancybox=False)
                
                plt.savefig(os.path.join(plots_dir, f'benchmark_{benchmark}_util_w{window_size}_s{step_size}.png'),
                           bbox_inches='tight', pad_inches=0.1)
                plt.close()


def calculate_cache_util_trend_bak(values, total, window_size, rate_type, step):
    if len(values) != len(total):
        raise ValueError("Values and total lists must have the same length")

    # Convert to numpy arrays, handling None values
    values_array = np.array([float(v) if v is not None else np.nan for v in values])
    total_array = np.array([float(t) if t is not None else np.nan for t in total])

    # Calculate differences if tracking changes over time
    values_diff = np.diff(values_array, prepend=0)
    
    # Initialize result array
    rates = np.full(len(values), np.nan)
    
    # Calculate rates for each window with the given step size
    for i in range(0, len(values), step):
        if i >= window_size - 1:
            # Get window slice
            window_slice = slice(max(0, i - window_size + 1), i + 1)
            
            # Calculate based on rate type
            if rate_type in ['cache_util', 'cache_reaccess']:
                if not np.isnan(total_array[i]) and total_array[i] > 0:
                    rates[i] = (values_array[i] / total_array[i])
            
            elif rate_type in ['ext_mem_util', 'ext_pages_util']:
                if not np.isnan(total_array[i]) and total_array[i] > 0:
                    rates[i] = (values_array[i] / total_array[i])

    return rates.tolist()


def plot_cache_util_trend_bak(cache_util_rates: List[float], 
                         cache_reaccess_rates: List[float],
                         ext_mem_rates: List[float],
                         ext_pages_rates: List[float],
                         zsim_dir: str, cache_name: str, 
                         window_size: int, step: int,
                         plot_path: str, 
                         x_values: np.ndarray, 
                         x_type: str,
                         ffi_points: List[int],  # Add ffi_points
                         warmup_instrs: int):  # Add warmup_instrs
    """Plot all utilization trends in a single figure using publication-quality style."""
    try:
        # Check if we have any valid data points after windowing
        has_data = any([
            check_valid_data_points(rates, window_size, step)
            for rates in [cache_util_rates, cache_reaccess_rates, ext_mem_rates, ext_pages_rates]
        ])
        
        if not has_data:
            output_filename = get_output_name(zsim_dir, cache_name=cache_name, 
                                           stat_type='util', window_size=window_size,
                                           step=step)
            title = f'Memory System Utilization: {cache_name}\n(Window Size: {window_size}, Step: {step})'
            plot_warning_graph(plot_path, output_filename, title)
            return

        # Update x_values length to match data length
        data_length = len(cache_util_rates)
        if len(x_values) > data_length:
            x_values = x_values[:data_length]
        elif len(x_values) < data_length:
            x_values = np.pad(x_values, (0, data_length - len(x_values)), 'edge')

        # Only plot points that meet the window size requirement
        valid_indices = [i for i in range(0, data_length, step) if i >= window_size - 1]
        if not valid_indices:
            reason = f"No valid data points with window={window_size} and step={step}.\nTry smaller values."
            plot_warning_graph(plot_path, output_filename, title, reason)
            return

        setup_plot_style()
        
        # Create single figure
        fig = plt.figure(figsize=(12, 6))
        ax = fig.add_subplot(111)
        
        # Get valid data points
        x_plot = x_values[valid_indices]
        
        # Plot all metrics with different colors and line styles
        handle_single_point_plot(ax, x_plot, 
                               np.array(cache_util_rates)[valid_indices]*100,
                               'Cache Utilization', '#1f77b4', '-')
        handle_single_point_plot(ax, x_plot, 
                               np.array(cache_reaccess_rates)[valid_indices]*100,
                               'Cache Re-access', '#2ca02c', '--')
        handle_single_point_plot(ax, x_plot, 
                               np.array(ext_mem_rates)[valid_indices]*100,
                               'Ext Memory Utilization', '#ff7f0e', ':')
        handle_single_point_plot(ax, x_plot, 
                               np.array(ext_pages_rates)[valid_indices]*100,
                               'Ext Pages Utilization', '#d62728', '-.')
        
        # Configure axis
        x_label = 'Billions of Instructions' if x_type == 'instr' else 'Period'
        ax.set_xlabel(x_label)
        ax.set_ylabel('Utilization (%)')
        ax.set_ylim(-0.02, 100.02)
        ax.grid(True, linestyle=':', alpha=0.5, color='#cccccc')
        
        # Add text annotations for ffiPoints and warmupInstrs
        text_str = f"Warmup Instructions: {warmup_instrs:.2e}\n"
        text_str += f"FFI Points: {ffi_points}"
        
        # Place a text box in upper left in axes coords
        ax.text(0.02, 0.98, text_str, transform=ax.transAxes, fontsize=10,
                verticalalignment='top', bbox=dict(facecolor='white', alpha=0.7))
        
        # Place legend outside plot
        box = ax.get_position()
        ax.set_position([box.x0, box.y0, box.width * 0.85, box.height])
        ax.legend(loc='center left', bbox_to_anchor=(1.05, 0.5),
                 frameon=True, edgecolor='black', fancybox=False)
        
        ax.set_title(f'Memory System Utilization: {cache_name}\n(Window Size: {window_size}, Step: {step})')
        
        # Save plot
        if not os.path.exists(plot_path):
            os.makedirs(plot_path)
            
        output_filename = get_output_name(zsim_dir, cache_name=cache_name, 
                                        stat_type='util', window_size=window_size,
                                        step=step)
        
        plt.savefig(os.path.join(plot_path, output_filename), 
                    bbox_inches='tight', pad_inches=0.1)
        
        # Save the data points
        save_plot_data({
            'cache_util_rates': cache_util_rates,
            'cache_reaccess_rates': cache_reaccess_rates,
            'ext_mem_rates': ext_mem_rates,
            'ext_pages_rates': ext_pages_rates,
            'x': x_values.tolist(),
            'x_label': x_label,
            'ffi_points': ffi_points,  # Save ffi_points
            'warmup_instrs': warmup_instrs  # Save warmup_instrs
        }, output_filename, plot_path)
        
        print(f"Plot saved to: {output_filename}")
        plt.show()
        plt.close()
        
    except Exception as e:
        print(f"Error creating utilization plot: {e}")


def read_config(filepath):
    """
    Reads the out.cfg file, extracts ffiPoints and warmupInstrs.
    Returns a dictionary containing the extracted parameters.
    """
    config = {}
    config['ffiPoints'] = []
    config['warmupInstrs'] = 0

    try:
        cfg_path = os.path.dirname(filepath) + "/out.cfg"
        with open(cfg_path, 'r') as f:
            content = f.read()
            
            # Parse ffiPoints using a more robust regex approach
            ffi_match = re.search(r'ffiPoints\s*=\s*"([^"]+)"', content)
            if ffi_match:
                ffi_str = ffi_match.group(1)
                try:
                    # Clean up the string and convert to integers
                    ffi_str = ffi_str.replace(';', '').strip()
                    config['ffiPoints'] = [int(x) for x in ffi_str.split()]
                    print(f"Successfully parsed ffiPoints: {config['ffiPoints']}")
                except ValueError as e:
                    print(f"Warning: Could not parse ffiPoints: {e}")
                    config['ffiPoints'] = []
            
            # Parse warmupInstrs
            warmup_match = re.search(r'warmupInstrs\s*=\s*(\d+)L?;?', content)
            if warmup_match:
                try:
                    # Extract just the digits
                    warmup_str = warmup_match.group(1)
                    config['warmupInstrs'] = int(warmup_str)
                    print(f"Successfully parsed warmupInstrs: {config['warmupInstrs']}")
                except ValueError as e:
                    print(f"Warning: Could not parse warmupInstrs: {e}")
                    config['warmupInstrs'] = 0
    except FileNotFoundError:
        print(f"Warning: Config file not found: {cfg_path}")
    except Exception as e:
        print(f"Error reading config file: {e}")
    
    return config


def main():
    parser = argparse.ArgumentParser(description='Parse and analyze ZSim statistics')
    
    # Required argument
    parser.add_argument('zsim_dir', help='Path to zsim output directory')
    
    # Optional arguments
    parser.add_argument('--stat-type', '-t', default='hit', 
                      choices=['hit', 'miss', 'thit', 'ipc', 'combine', 'custom', 'util'],
                      help='Type of statistic to analyze (default: hit)')
    parser.add_argument('--path', '-p', 
                      help='Custom path to analyze (e.g., "root.l2.hGETS")')
    parser.add_argument('--window-size', '-w', type=int, default=1,
                      help='Size of the sliding window (default: 1)')
    parser.add_argument('--step', '-s', type=int, default=1,
                      help='Step size for data points (default: 1)')
    parser.add_argument('--plot', action='store_true',
                      help='Enable plotting')
    parser.add_argument('--verbose', '-v', action='store_true',
                      help='Enable verbose output')
    parser.add_argument('--use-h5', '-h5', action='store_true',
                      help='Use HDF5 file format instead of text')
    parser.add_argument('--rate-type', '-rt', choices=['instr', 'phase'], default='instr',
                      help='Type of rate calculation (instruction-based or phase-based')
    
    args = parser.parse_args()

    # Set global verbose flag
    set_verbose(args.verbose)

    start_time = time.time()
    zsim_dir = args.zsim_dir
    plot_path = os.path.join(zsim_dir, "..", "plots")
    stat_type = args.stat_type
    custom_path = args.path
    window_size = args.window_size
    step = args.step
    plot_enabled = args.plot
    use_h5 = args.use_h5
    rate_type = args.rate_type

    if use_h5:
        zsim_file = os.path.join(zsim_dir, "zsim.h5")
    else:
        zsim_file = os.path.join(zsim_dir, "zsim-pout.out")
    
    # If custom path is provided but stat_type isn't 'custom', warn user
    if custom_path and stat_type != 'custom':
        print(f"Warning: Custom path provided but stat-type is '{stat_type}'. Did you mean to use '-t custom'?")
    
    # If stat_type is 'custom' but no path provided, error out
    if stat_type == 'custom' and not custom_path:
        print("Error: Custom stat type requires a path (use --path)")
        sys.exit(1)
    
    # Parse data for custom path lookup
    if stat_type == 'custom':
        print(f"Calculating CUSTOM stat for {zsim_dir}...")
        if not os.path.exists(zsim_file):
            print(f"Error: zsim-pout.out not found in directory {zsim_dir}")
            sys.exit(1)
        data = parse_zsim_output(zsim_file, use_h5=use_h5)
        debug_print(f"Looking up values for path: {custom_path}")
        values = []
        for period in data:
            value = get_value_by_path(period, custom_path)
            values.append(value)
            
        # Filter out None values for average calculation
        valid_values = [v for v in values if v is not None]
        
        print(f"\nValues for path '{custom_path}':")
        if valid_values:
            print(f"Found {len(valid_values)} valid values out of {len(values)} periods")
            print(f"First few values: {valid_values[:10]}...")
            avg = sum(valid_values) / len(valid_values)
            print(f"\nAverage: {avg:.4f}")
        else:
            print("No valid values found. Check if the path is correct.")
            print("Available paths in first period:")
            if data and len(data) > 0:
                def print_paths(d, prefix=""):
                    if isinstance(d, dict):
                        for k, v in d.items():
                            new_prefix = f"{prefix}.{k}" if prefix else k
                            print(new_prefix)
                            print_paths(v, new_prefix)
                print_paths(data[0])
    
    # Rest of the stat types remain the same
    elif stat_type in ["hit", "miss", "thit"]:
        print(f"Calculating HIT/MISS rates for {zsim_dir}...")
        # Parse file once to find cache paths
        if not os.path.exists(zsim_file):
            print(f"Error: zsim_file not found in directory {zsim_dir}")
            sys.exit(1)
        
        data = parse_zsim_output(zsim_file, use_h5=use_h5)

        # Read config file to get ffiPoints and warmupInstrs
        config = read_config(zsim_file)
        ffi_points = config.get('ffiPoints', [])
        warmup_instrs = config.get('warmupInstrs', 0)

        print(f"FFI Points: {ffi_points}")
        print(f"Warmup Instructions: {warmup_instrs}")

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

        # After getting the extracted values:
        if VERBOSE:
            print("\nRaw values:")
            print(f"Load hits: {load_hits[:10]}...")
            print(f"Load misses: {load_misses[:10]}...")
            print(f"Store hits: {store_hits[:10]}...")
            print(f"Store misses: {store_misses[:10]}...")

        # Calculate rates with step parameter
        read_hit_rates = calculate_cache_rate_trend(load_hits, load_misses, window_size, 'hit', step)
        read_miss_rates = calculate_cache_rate_trend(load_hits, load_misses, window_size, 'miss', step)
        total_hit_rates = calculate_total_hit_rate_trend(load_hits, load_misses, store_hits, store_misses, window_size, step)

        if VERBOSE:
            print(f"\nRead hit rate trend (window={window_size}, step={step}): ...({window_size}){read_hit_rates[window_size-1:window_size+9]}...")
            print(f"Read miss rate trend (window={window_size}, step={step}): ...({window_size}){read_miss_rates[window_size-1:window_size+9]}...")
            print(f"Total hit rate trend (window={window_size}, step={step}): ...({window_size}){total_hit_rates[window_size-1:window_size+9]}...")

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
        
        # Get x-axis values based on rate type
        if rate_type == 'instr':
            total_instrs = get_total_instructions(data, use_h5=use_h5)
            if len(total_instrs) == 0:
                print("Warning: Could not get instruction counts, falling back to phase-based")
                rate_type = 'phase'
        
        # Plot with appropriate x-axis
        if plot_enabled:
            try:
                if rate_type == 'instr':
                    x_values = total_instrs / 1e9  # Convert to billions
                else:
                    x_values = np.arange(len(read_hit_rates))
                    
                plot_cache_rate_trend(read_hit_rates, read_miss_rates, total_hit_rates,
                                    zsim_dir, cache_name, window_size, step, plot_path,
                                    x_values=x_values, x_type=rate_type,
                                    ffi_points=ffi_points,  # Pass the ffi_points variable
                                    warmup_instrs=warmup_instrs)  # Pass the warmup_instrs variable
            except Exception as e:
                print(f"Unable to create plot: {e}")

    # Check if we should calculate IPC
    elif stat_type == "ipc":
        print(f"Calculating IPC for {zsim_dir}...")
        if not os.path.exists(zsim_file):
            print(f"Error: zsim_file not found in directory {zsim_dir}")
            sys.exit(1)
        ipc_data, overall_ipc, x_values, actual_rate_type = calculate_ipc(zsim_file, "root", 
                                                                         window_size, step, 
                                                                         use_h5=use_h5,
                                                                         rate_type=rate_type)
        # Read config file to get ffiPoints and warmupInstrs
        config = read_config(zsim_file)
        ffi_points = config.get('ffiPoints', [])
        warmup_instrs = config.get('warmupInstrs', 0)

        print(f"FFI Points: {ffi_points}")
        print(f"Warmup Instructions: {warmup_instrs}")

        if not ipc_data:
            print("No core paths found with cycles, and instrs stats")
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
            plot_ipc_trend(ipc_data, overall_ipc, zsim_dir, window_size, step, plot_path,
                          x_values=x_values, x_type=actual_rate_type,
                          ffi_points=ffi_points,  # Pass the ffi_points variable 
                          warmup_instrs=warmup_instrs)  # Pass the warmup_instrs variable

    elif stat_type == "util":
        print(f"Calculating utilization statistics for {zsim_dir}...")
        if not os.path.exists(zsim_file):
            print(f"Error: zsim_file not found in directory {zsim_dir}")
            sys.exit(1)
            
        data = parse_zsim_output(zsim_file, use_h5=use_h5)

        # Read config file to get ffiPoints and warmupInstrs
        config = read_config(zsim_file)
        ffi_points = config.get('ffiPoints', [])
        warmup_instrs = config.get('warmupInstrs', 0)

        print(f"FFI Points: {ffi_points}")
        print(f"Warmup Instructions: {warmup_instrs}")

        cache_paths = find_cache_paths(data)
        
        if not cache_paths:
            print("No cache paths found under root.mem.mem-0")
            sys.exit(1)
            
        # Use the first cache found
        base_path = cache_paths[0]
        cache_name = base_path.split('.')[-1]
        print(f"Found cache: {cache_name}")
        
        # Get all relevant cache access stats
        reaccessed_path = f"{base_path}.numReaccessedLines"
        accessed_path = f"{base_path}.numAccessedLines"
        total_path = f"{base_path}.numTotalLines"
        ext_accessed_path = f"{base_path}.numAccessedExtLines"
        ext_total_path = f"{base_path}.numTotalExtLines"
        ext_pages_accessed_path = f"{base_path}.numAccessedExtPages"
        ext_pages_total_path = f"{base_path}.numTotalExtPages"
        
        print(f"Analyzing utilization statistics for {base_path}")
        
        # Get both load and store hits/misses 
        paths_to_extract = [reaccessed_path, accessed_path, total_path, ext_accessed_path, ext_total_path, ext_pages_accessed_path, ext_pages_total_path]
        extracted_values = get_multiple_values(data, paths_to_extract)
        
        reaccessed = extracted_values[reaccessed_path]
        accessed = extracted_values[accessed_path]
        cache_total = extracted_values[total_path]
        ext_accessed = extracted_values[ext_accessed_path]
        ext_total = extracted_values[ext_total_path]
        ext_pages_accessed = extracted_values[ext_pages_accessed_path]
        ext_pages_total = extracted_values[ext_pages_total_path]

        # After getting the extracted values:
        if VERBOSE:
            print("\nRaw values:")
            print(f"Cache re-accessed: {reaccessed[:10]}...")
            print(f"Cache accessed: {accessed[:10]}...")
            print(f"Cache total: {cache_total[:10]}...")
            print(f"Ext accessed: {ext_accessed[:10]}...")
            print(f"Ext total: {ext_total[:10]}...")
            print(f"Ext pages accessed: {ext_pages_accessed[:10]}...")
            print(f"Ext pages total: {ext_pages_total[:10]}...")

        cache_util_rates = calculate_cache_util_trend(accessed, cache_total, window_size, 'cache_util', step)
        cache_reaccess_rates = calculate_cache_util_trend(reaccessed, cache_total, window_size, 'cache_reaccess', step)
        ext_mem_util_rates = calculate_cache_util_trend(ext_accessed, ext_total, window_size, 'ext_mem_util', step)
        ext_pages_util_rates = calculate_cache_util_trend(ext_pages_accessed, ext_pages_total, window_size, 'ext_pages_util', step)
        
        if VERBOSE:
            print(f"\nCache utilization rates (window={window_size}, step={step}): ...({window_size}){cache_util_rates[window_size-1:window_size+9]}...")
            print(f"Cache reaccess rates (window={window_size}, step={step}): ...({window_size}){cache_reaccess_rates[window_size-1:window_size+9]}...")
            print(f"External memory utilization rates (window={window_size}, step={step}): ...({window_size}){ext_mem_util_rates[window_size-1:window_size+9]}...")
            print(f"External pages utilization rates (window={window_size}, step={step}): ...({window_size}){ext_pages_util_rates[window_size-1:window_size+9]}...")
            
        # Get x-axis values based on rate type
        if rate_type == 'instr':
            total_instrs = get_total_instructions(data, use_h5=use_h5)
            if len(total_instrs) == 0:
                print("Warning: Could not get instruction counts, falling back to phase-based")
                rate_type = 'phase'
        
        # Plot with appropriate x-axis
        if plot_enabled:
            try:
                if rate_type == 'instr':
                    x_values = total_instrs / 1e9  # Convert to billions
                else:
                    x_values = np.arange(len(cache_util_rates))
                    
                plot_cache_util_trend(cache_util_rates, cache_reaccess_rates, ext_mem_util_rates, ext_pages_util_rates,
                                    zsim_dir, cache_name, window_size, step, plot_path,
                                    x_values=x_values, x_type=rate_type,
                                    ffi_points=ffi_points,  # Pass ffi_points
                                    warmup_instrs=warmup_instrs)  # Pass warmup_instrs
            except Exception as e:
                print(f"Unable to create plot: {e}")

    elif stat_type == "combine":
        print(f"Calculating COMBINED for {zsim_dir}...")
        plot_path = os.path.join(zsim_dir, "plots")
        combine_plots(plot_path)
        
    end_time = time.time()
    debug_print(f"Total execution time: {end_time - start_time:.2f} seconds")


if __name__ == "__main__":
    main()
