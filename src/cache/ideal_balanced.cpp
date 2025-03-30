#include "cache/ideal_balanced.h"

#include <cstdlib>  // For std::rand
#include <vector>

#include "mc.h"

uint64_t IdealBalancedScheme::access(MemReq& req) {
    // Determine request type
    ReqType type = (req.type == GETS || req.type == GETX) ? LOAD : STORE;
    Address address = req.lineAddr % _ext_size;

    uint32_t mcdram_select = 0;
    Address mc_address = address;
    uint64_t set_num = 0;
    Address tag = mc_address;
    uint64_t line_num = tag;

    // info("phy_addr = 0x%lx, cache_addr = 0x%lx, set_num = %ld, tag = 0x%lx, line_num = %ld\n", address, mc_address, set_num, tag, line_num);

    // Check for cache hit
    uint32_t hit_way = _num_ways;
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
            _num_hit_per_step++;
            _numLoadHit.inc();
            data_ready_cycle = req.cycle;  // Data available after cache latency
        } else {
            // Cache miss: Fetch from main memory and fill the cache
            _num_miss_per_step++;
            _numLoadMiss.inc();

            // Fetch data from main memory
            MemReq main_memory_req = {address, GETS, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
            data_ready_cycle = _mc->_ext_dram->access(main_memory_req, 1, 4);
            _ext_bw_per_step += 4;

            // Fill cache
            uint32_t victim_way = _num_ways;
            if (_line_entries[line_num].way < _num_ways) {
                victim_way = _line_entries[line_num].way;
            } else {
                victim_way = _current_way;
                _current_way = (_current_way + 1) % _num_ways;
                _line_entries[line_num].way = victim_way;
            }

            // Handle eviction if victim is dirty
            if (_cache[set_num].ways[victim_way].valid && _cache[set_num].ways[victim_way].dirty) {
                Address wb_address = _cache[set_num].ways[victim_way].tag * _granularity;
                MemReq wb_req = {wb_address, PUTX, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
                _mc->_ext_dram->access(wb_req, 2, 4);  // Write-back to main memory
                _ext_bw_per_step += 4;
            }

            // Insert new line (fill operation)
            _cache[set_num].ways[victim_way].tag = tag;
            _cache[set_num].ways[victim_way].valid = true;
            _cache[set_num].ways[victim_way].dirty = false;  // LOAD: line is clean
        }
    } else {  // STORE
        // Simulate cache write access
        MemReq write_req = {mc_address, PUTX, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
        req.cycle = _mc->_mcdram[mcdram_select]->access(write_req, 0, 4);
        _mc_bw_per_step += 4;

        if (hit_way < _num_ways) {
            // Write hit
            _num_hit_per_step++;
            _numStoreHit.inc();
            _cache[set_num].ways[hit_way].dirty = true;
            data_ready_cycle = req.cycle;
        } else {
            // Write miss
            _num_miss_per_step++;
            _numStoreMiss.inc();

            // Fill cache
            uint32_t victim_way = _num_ways;
            if (_line_entries[line_num].way < _num_ways) {
                victim_way = _line_entries[line_num].way;
            } else {
                victim_way = _current_way;
                _current_way = (_current_way + 1) % _num_ways;
                _line_entries[line_num].way = victim_way;
            }

            // Handle eviction if victim is dirty
            if (_cache[set_num].ways[victim_way].valid && _cache[set_num].ways[victim_way].dirty) {
                Address wb_address = _cache[set_num].ways[victim_way].tag * _granularity;
                MemReq wb_req = {wb_address, PUTX, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
                _mc->_ext_dram->access(wb_req, 2, 4);  // Write-back to main memory, non-critical
                _ext_bw_per_step += 4;
            }
            // Insert new line
            _cache[set_num].ways[victim_way].tag = tag;
            _cache[set_num].ways[victim_way].valid = true;
            _cache[set_num].ways[victim_way].dirty = true;  // STORE: mark as dirty
            data_ready_cycle = req.cycle;
        }
    }

    return data_ready_cycle;
}

void IdealBalancedScheme::period(MemReq& req) {
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

void IdealBalancedScheme::initStats(AggregateStat* parentStat) {
    AggregateStat* stats = new AggregateStat();
    stats->init("idealBalancedCache", "IdealBalanced Cache stats");
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
    parentStat->append(stats);
}