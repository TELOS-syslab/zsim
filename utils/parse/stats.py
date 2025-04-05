# zsim stats README
# Author: Daniel Sanchez <sanchezd@stanford.edu>
# Date: May 3 2011
#
# Stats are now saved in HDF5, and you should never need to write a stats
# parser. This README explains how to access them in python using h5py. It
# doubles as a python script, so you can just execute it with "python
# README.stats" and see how everything works (after you have generated a stats
# file).
#
import os
import h5py # presents HDF5 files as numpy arrays
import numpy as np
from pprint import pprint


def print_hdf5_structure(h5_file, level=0):
    """Recursively print the structure of an HDF5 file."""
    for key, value in h5_file.items():
        print("  " * level + f"{key}:")
        if isinstance(value, h5py.Dataset):
            print("  " * (level + 1) + f"Dataset, Shape: {value.shape}, Type: {value.dtype}")
        elif isinstance(value, h5py.Group):
            print_hdf5_structure(value, level + 1)
def h5_tree(val, pre=''):
    items = len(val)
    for key, val in val.items():
        items -= 1
        if items == 0:
            # the last item
            if type(val) == h5py._hl.group.Group:
                print(pre + '└── ' + key)
                h5_tree(val, pre+'    ')
            else:
                try:
                    print(pre + '└── ' + key + ' (%d)' % len(val))
                except TypeError:
                    print(pre + '└── ' + key + ' (scalar)')
        else:
            if type(val) == h5py._hl.group.Group:
                print(pre + '├── ' + key)
                h5_tree(val, pre+'│   ')
            else:
                try:
                    print(pre + '├── ' + key + ' (%d)' % len(val))
                except TypeError:
                    print(pre + '├── ' + key + ' (scalar)')
                    
def parse_zsim_h5(file_path: str):
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
                    
# Open stats file
curpath = os.path.dirname(os.path.realpath(__file__))
f = h5py.File(os.path.join(curpath,'../../output/20250402-142905[chamo-johnny_lu_page]/zsim.h5'), 'r')
print_hdf5_structure(f)
h5_tree(f)

# Get the single dataset in the file
dset = f["stats"]
pprint(dset['root']['mem']['mem-0'][-1])

data = parse_zsim_h5(os.path.join(curpath,'../../output/20250402-142905[chamo-johnny_lu_page]/zsim.h5'))
pprint(data[-1])
exit()
# Each dataset is first indexed by record (sample). A record is a *snapshot* of all the
# stats taken at a specific time.  All stats files have at least two records,
# at beginning (dest[0])and end of simulation (dset[-1]).  Inside each record,
# the format follows the structure of the simulated objects. A few examples:

# Phase count at end of simulation
endPhase = dset[-1]['phase']
print(endPhase)

# If your L2 has a single bank, this is all the L2 hits. Otherwise it's the
# hits of the first L2 bank
l2_0_hits = dset[-1]['l2'][0]['hGETS'] + dset[-1]['l2'][0]['hGETX']
print(l2_0_hits)

# Hits into all L2s
l2_hits = np.sum(dset[-1]['l2']['hGETS'] + dset[-1]['l2']['hGETX'])
print(l2_hits)

# You can also focus on one sample, or index over multiple steps, e.g.,
lastSample = dset[-1]
allHitsS = lastSample['l2']['hGETS']
firstL2HitsS = allHitsS[0]
print(firstL2HitsS)

# There is a certain slack in the positions of numeric and non-numeric indices,
# so the following are equivalent:
print(dset[-1]['l2'][0]['hGETS']) 
#print dset[-1][0]['l2']['hGETS'] # can't do
print(dset[-1]['l2']['hGETS'][0])
print(dset['l2']['hGETS'][-1,0])
print(dset['l2'][-1,0]['hGETS'])
print(dset['l2']['hGETS'][-1,0])

# However, you can't do things like dset[-1][0]['l2']['hGETS'], because the [0]
# indexes a specific element in array 'l2'. The rule of thumb seems to be that
# numeric indices can "flow up", i.e., you can index them later than you should.
# This introduces no ambiguities.

# Slicing works as in numpy, e.g.,
print(dset['l2']['hGETS']) # a 2D array with samples*per-cache data
print(dset['l2']['hGETS'][-1]) # a 1D array with per-cache numbers, for the last sample
print(dset['l2']['hGETS'][:,0]) # 1D array with all samples, for the first L2 cache

# OK, now go bananas!

