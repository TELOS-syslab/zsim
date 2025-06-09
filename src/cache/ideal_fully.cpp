#include "cache/ideal_fully.h"

#include <algorithm>  // For std::find
#include <cstdlib>    // For std::rand
#include <vector>

#include "mc.h"

// Update LRU state when a way is accessed - O(1) operation
void IdealFullyScheme::updateLRU(uint64_t way) {
    if (way == _mru_way) return;  // Already MRU

    // Remove from current position
    uint64_t prev = _lru_array[way].prev;
    uint64_t next = _lru_array[way].next;
    _lru_array[prev].next = next;
    _lru_array[next].prev = prev;

    if (way == _lru_way) {
        _lru_way = prev;
    }

    // Insert at MRU position
    uint64_t old_prev_of_mru = _lru_array[_mru_way].prev;  // Store this before modifying
    _lru_array[way].next = _mru_way;
    _lru_array[way].prev = old_prev_of_mru;
    _lru_array[_mru_way].prev = way;
    _lru_array[old_prev_of_mru].next = way;
    _mru_way = way;
}

// Get the least recently used way - O(1) operation
uint64_t IdealFullyScheme::getLRUWay() {
    return _lru_way;
}

uint64_t IdealFullyScheme::access(MemReq& req) {
    // Determine request type
    ReqType type = (req.type == GETS || req.type == GETX) ? LOAD : STORE;
    // Address address = req.lineAddr % (_ext_size / 64);
    Address address = req.lineAddr;

    uint32_t mcdram_select = 0;
    Address mc_address = address;
    uint64_t set_num = 0;
    Address tag = mc_address;
    uint64_t line_num = tag;

    _accessed_ext_lines_set.insert(address);
    _accessed_ext_lines = _accessed_ext_lines_set.size();
    _accessed_ext_pages_set.insert(address / (_page_size / 64));
    _accessed_ext_pages = _accessed_ext_pages_set.size();

    // info("phy_addr = 0x%lx, cache_addr = 0x%lx, set_num = %ld, tag = 0x%lx, line_num = %ld\n", address, mc_address, set_num, tag, line_num);

    // Check for cache hit
    uint64_t hit_way = _num_ways;
    if (_line_entries[line_num].way < _num_ways) {
        hit_way = _line_entries[line_num].way;
        if (!(_cache[set_num].ways[hit_way].valid && _cache[set_num].ways[hit_way].tag == tag)) {
            hit_way = _num_ways;
        }
    }

    uint64_t data_ready_cycle = 0;
    MESIState state;

    if (type == LOAD) {
        // Simulate cache access (in-subarray tag matching)
        MemReq read_req = {mc_address, GETS, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
        req.cycle = _mc->_mcdram[mcdram_select]->access(read_req, 0, 4);
        _mc_bw_per_step += 4;

        if (hit_way < _num_ways) {
            // Cache hit
            updateUtilizationStats(set_num, hit_way);
            _num_hit_per_step++;
            _numLoadHit.inc();
            data_ready_cycle = req.cycle;  // Data available after cache latency

            // Update LRU - move this way to most recently used position
            updateLRU(hit_way);
        } else {
            // Cache miss: Fetch from main memory and fill the cache
            _num_miss_per_step++;
            _numLoadMiss.inc();

            // Fetch data from main memory
            MemReq main_memory_req = {address, GETS, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
            data_ready_cycle = _mc->_ext_dram->access(main_memory_req, 1, 4);
            _ext_bw_per_step += 4;

            // Select victim way using LRU policy
            uint64_t victim_way;
            // Get the least recently used way
            victim_way = getLRUWay();
            _line_entries[line_num].way = victim_way;

            // Handle eviction if victim is dirty
            if (_cache[set_num].ways[victim_way].valid && _cache[set_num].ways[victim_way].dirty) {
                _numDirtyEviction.inc();
                Address wb_address = _cache[set_num].ways[victim_way].tag * _granularity;
                MemReq wb_req = {wb_address, PUTX, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
                _mc->_ext_dram->access(wb_req, 2, 4);  // Write-back to main memory
                _ext_bw_per_step += 4;
            } else if (_cache[set_num].ways[victim_way].valid) {
                _numCleanEviction.inc();
            }

            // Insert new line (fill operation)
            _cache[set_num].ways[victim_way].tag = tag;
            _cache[set_num].ways[victim_way].valid = true;
            _cache[set_num].ways[victim_way].dirty = false;  // LOAD: line is clean
            updateUtilizationStats(set_num, victim_way);
            // Update LRU - move this way to most recently used position
            updateLRU(victim_way);
        }
    } else {  // STORE
        // Simulate cache write access
        MemReq write_req = {mc_address, PUTX, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
        req.cycle = _mc->_mcdram[mcdram_select]->access(write_req, 0, 4);
        _mc_bw_per_step += 4;

        if (hit_way < _num_ways) {
            // Write hit
            updateUtilizationStats(set_num, hit_way);
            _num_hit_per_step++;
            _numStoreHit.inc();
            _cache[set_num].ways[hit_way].dirty = true;
            data_ready_cycle = req.cycle;

            // Update LRU - move this way to most recently used position
            updateLRU(hit_way);
        } else {
            // Write miss
            _num_miss_per_step++;
            _numStoreMiss.inc();

            // Select victim way using LRU policy
            uint64_t victim_way;
            // Get the least recently used way
            victim_way = getLRUWay();
            _line_entries[line_num].way = victim_way;

            // Handle eviction if victim is dirty
            if (_cache[set_num].ways[victim_way].valid && _cache[set_num].ways[victim_way].dirty) {
                _numDirtyEviction.inc();
                Address wb_address = _cache[set_num].ways[victim_way].tag * _granularity;
                MemReq wb_req = {wb_address, PUTX, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
                _mc->_ext_dram->access(wb_req, 2, 4);  // Write-back to main memory
                _ext_bw_per_step += 4;
            } else if (_cache[set_num].ways[victim_way].valid) {
                _numCleanEviction.inc();
            }

            // Insert new line
            _cache[set_num].ways[victim_way].tag = tag;
            _cache[set_num].ways[victim_way].valid = true;
            _cache[set_num].ways[victim_way].dirty = true;  // STORE: mark as dirty
            data_ready_cycle = req.cycle;
            updateUtilizationStats(set_num, victim_way);
            // Update LRU - move this way to most recently used position
            updateLRU(victim_way);
        }
    }

    return data_ready_cycle;
}

void IdealFullyScheme::period(MemReq& req) {
    if (_stats_period && _num_requests % _stats_period == 0) {
        logUtilizationStats();
        // Reset access counts after logging
        for (uint64_t i = 0; i < _total_lines; i++) {
             _line_access_count[i] &= ((1ULL << 32) - 1);
        }
    }
    // Handle bandwidth balance if needed
    if (_bw_balance && _num_requests % _step_length == 0) {
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

                        for (uint64_t way = 0; way < _num_ways; way++) {
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
}

void IdealFullyScheme::initStats(AggregateStat* parentStat) {
    AggregateStat* stats = new AggregateStat();
    stats->init("idealFullyCache", "Fully Associative Cache with LRU stats");
    _numCleanEviction.init("cleanEvict", "Clean Eviction");
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
    
    stats->append(_numReaccessedLines);
    stats->append(_numAccessedLines);
    stats->append(_numTotalLines);
    stats->append(_numAccessedExtLines);
    stats->append(_numTotalExtLines);
    stats->append(_numAccessedExtPages);
    stats->append(_numTotalExtPages);
    
    parentStat->append(stats);
}