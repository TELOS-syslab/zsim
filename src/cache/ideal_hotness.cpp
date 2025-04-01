#include "cache/ideal_hotness.h"

#include <cstdlib>  // For std::rand
#include <algorithm> // For std::find
#include <vector>

#include "mc.h"

// Update LRU state when a way is accessed - O(1) operation
void IdealHotnessScheme::updateLRU(uint32_t way) {
    if (way == _mru_way) return; // Already MRU
    
    // Remove from current position
    uint32_t prev = _lru_array[way].prev;
    uint32_t next = _lru_array[way].next;
    _lru_array[prev].next = next;
    _lru_array[next].prev = prev;
    
    if (way == _lru_way) {
        _lru_way = prev;
    }
    
    // Insert at MRU position
    _lru_array[way].next = _mru_way;
    _lru_array[way].prev = _lru_array[_mru_way].prev;
    _lru_array[_mru_way].prev = way;
    _lru_array[_lru_array[way].prev].next = way;
    _mru_way = way;
}

// Get the least recently used way - O(1) operation
uint32_t IdealHotnessScheme::getLRUWay() {
    return _lru_way;
}

uint64_t IdealHotnessScheme::getPageNumber(uint64_t lineAddr) {
    return lineAddr / _lines_per_page;
}

uint64_t IdealHotnessScheme::getPageOffset(uint64_t lineAddr) {
    return lineAddr % _lines_per_page;
}

void IdealHotnessScheme::incrementFrequency(uint32_t pageIndex) {
    if (_page_table[pageIndex].frequency < UINT32_MAX) {
        _page_table[pageIndex].frequency++;
    }
}

uint32_t IdealHotnessScheme::findVictimPage() {
    uint32_t victim = 0;
    uint32_t lowest_freq = UINT32_MAX;
    
    // Find page with lowest frequency
    for (uint32_t i = 0; i < _num_pages; i++) {
        if (!_page_table[i].valid) {
            return i;  // Use empty page if available
        }
        if (_page_table[i].frequency < lowest_freq) {
            lowest_freq = _page_table[i].frequency;
            victim = i;
        }
    }
    return victim;
}

void IdealHotnessScheme::decayFrequencies() {
    // Decay all frequencies by half periodically
    for (uint32_t i = 0; i < _num_pages; i++) {
        _page_table[i].frequency >>= 1;
    }
}

uint64_t IdealHotnessScheme::access(MemReq& req) {
    uint64_t page_number = getPageNumber(req.lineAddr);
    uint64_t page_offset = getPageOffset(req.lineAddr);
    bool is_write = (req.type != GETS && req.type != GETX);
    
    auto it = _page_location.find(page_number);
    uint64_t data_ready_cycle = req.cycle;
    MESIState state;

    if (it != _page_location.end()) {
        // Page hit
        uint32_t page_index = it->second;
        incrementFrequency(page_index);
        
        if (is_write) {
            _numStoreHit.inc();
            _page_table[page_index].dirty = true;
        } else {
            _numLoadHit.inc();
        }
        _num_hit_per_step++;
        
    } else {
        // Page miss
        if (is_write) {
            _numStoreMiss.inc();
        } else {
            _numLoadMiss.inc();
        }
        _num_miss_per_step++;

        // Find victim page
        uint32_t victim_index = findVictimPage();
        
        // Handle eviction if necessary
        if (_page_table[victim_index].valid) {
            if (_page_table[victim_index].dirty) {
                _numDirtyEviction.inc();
                // Write back entire page
                for (uint32_t i = 0; i < _lines_per_page; i++) {
                    Address wb_addr = (_page_table[victim_index].tag * _lines_per_page + i) * _granularity;
                    MemReq wb_req = {wb_addr, PUTX, req.childId, &state, data_ready_cycle,
                                   req.childLock, req.initialState, req.srcId, req.flags};
                    data_ready_cycle = _mc->_ext_dram->access(wb_req, 2, 4);
                }
            } else {
                _numCleanEviction.inc();
            }
            // Remove old page mapping
            _page_location.erase(_page_table[victim_index].tag);
        }

        // Load new page
        for (uint32_t i = 0; i < _lines_per_page; i++) {
            Address load_addr = (page_number * _lines_per_page + i) * _granularity;
            MemReq load_req = {load_addr, GETS, req.childId, &state, data_ready_cycle,
                              req.childLock, req.initialState, req.srcId, req.flags};
            data_ready_cycle = _mc->_ext_dram->access(load_req, 1, 4);
        }

        // Update page table and mapping
        _page_table[victim_index].valid = true;
        _page_table[victim_index].dirty = is_write;
        _page_table[victim_index].tag = page_number;
        _page_table[victim_index].frequency = 1;
        _page_location[page_number] = victim_index;
    }

    return data_ready_cycle;
}

void IdealHotnessScheme::period(MemReq& req) {
    // Decay frequencies periodically
    decayFrequencies();
    
    // Reset per-step counters
    _num_hit_per_step /= 2;
    _num_miss_per_step /= 2;
    _mc_bw_per_step /= 2;
    _ext_bw_per_step /= 2;

    if (_bw_balance && _mc_bw_per_step + _ext_bw_per_step > 0) {
        // Calculate current bandwidth ratio
        double ratio = 1.0 * _mc_bw_per_step / (_mc_bw_per_step + _ext_bw_per_step);
        double target_ratio = 0.8;  // Target ratio (mc_bw = 4 * ext_bw)

        // Adjust _ds_index based on bandwidth difference
        uint64_t index_step = _num_sets / 1000;
        int64_t delta_index = (ratio - target_ratio > -0.02 && ratio - target_ratio < 0.02) ? 0 : index_step * (ratio - target_ratio) / 0.01;

        printf("ratio = %f\n", ratio);

        if (delta_index > 0) {
            // Handle increasing _ds_index
            for (uint32_t mc = 0; mc < _mc->_mcdram_per_mc; mc++) {
                for (uint64_t set = _ds_index; set < (uint64_t)(_ds_index + delta_index); set++) {
                    if (set >= _num_sets) break;

                    for (uint32_t way = 0; way < _num_ways; way++) {
                        Way& meta = _cache[set].ways[way];
                        if (meta.valid && meta.dirty) {
                            // Write back to external DRAM
                            MESIState state;
                            MemReq load_req = {meta.tag * 64, GETS, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
                            _mc->_mcdram[mc]->access(load_req, 2, (_granularity / 64) * 4);
                            MemReq wb_req = {meta.tag * 64, PUTX, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
                            _mc->_ext_dram->access(wb_req, 2, (_granularity / 64) * 4);
                            _ext_bw_per_step += (_granularity / 64) * 4;
                            _mc_bw_per_step += (_granularity / 64) * 4;
                        }

                        meta.valid = false;
                        meta.dirty = false;
                    }
                }
            }
        }
        _ds_index = ((int64_t)_ds_index + delta_index <= 0) ? 0 : _ds_index + delta_index;
        printf("_ds_index = %ld/%ld\n", _ds_index, _num_sets);
    }
}

void IdealHotnessScheme::initStats(AggregateStat* parentStat) {
    AggregateStat* stats = new AggregateStat();
    stats->init("idealHotnessCache", "Page-Based Cache with Frequency Replacement stats");
    _numCleanEviction.init("cleanEvict", "Clean Page Evictions");
    stats->append(&_numCleanEviction);
    _numDirtyEviction.init("dirtyEvict", "Dirty Eviction");
    stats->append(&_numDirtyEviction);
    _numLoadHit.init("loadHit", "Load Hit");
    stats->append(&_numLoadHit);
    _numLoadMiss.init("loadMiss", "Load Miss");
    stats->append(&_numLoadMiss);
    _numStoreHit.init("storeHit", "Store Hit");
    stats->append(&_numStoreHit);
    _numStoreMiss.init("storeMiss", "Store Miss");
    stats->append(&_numStoreMiss);
    parentStat->append(stats);
}