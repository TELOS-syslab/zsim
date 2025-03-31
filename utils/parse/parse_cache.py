import os
import numpy as np
import zsimparse
from zsimparse.util import format_names as iname
from zsimparse.base_data_dict import BaseDataDict
from zsimparse.h5stat import H5Stat
from zsimparse import util

class MemStat(H5Stat):
    '''
    Cache event counters of h5 stat.
    '''

    OTHER_COUNTERS = ('PUTS', 'INVX')

    def __init__(self, stat, cache_names):
        '''
        Construct from a h5 stat with the names of the cache.
        '''
        if not isinstance(stat, BaseDataDict):
            stat = BaseDataDict(stat)
        raw = stat.get(cache_names)
        if raw is None:
            raise ValueError('{}: {}: cannot get cache {} from h5 stat'
                             .format(util.PACKAGE_NAME,
                                     self.__class__.__name__,
                                     cache_names))
        super().__init__(raw)
        dummy = self.get('mGETS')  # used to decide shape and type
        if dummy is None:
            raise ValueError('{}: {}: {} is not a cache stat'
                             .format(util.PACKAGE_NAME,
                                     self.__class__.__name__,
                                     cache_names))
        self.cnt_shape = dummy.shape
        self.cnt_dtype = dummy.dtype


    def get_count(self, *events):
        ''' Get cache event counts. '''
        cnt = np.zeros(self.cnt_shape, dtype=self.cnt_dtype)
        for e in events:
            c = self.get(e)
            if c is not None:
                cnt += c
        return cnt

def parse_mem():
    simdir = os.path.join(os.path.abspath(os.path.dirname(__file__)), 'simdir')
    cfg = zsimparse.get_config_by_dir(simdir)
    dset = zsimparse.get_hdf5_by_dir(simdir, final_only=True)

    # Use the full path to the memory controller
    mem_path = iname(['mem', 0])
    mem = MemStat(dset, ('mem', 0))

if __name__ == '__main__':
    # parse()
    parse_mem()
