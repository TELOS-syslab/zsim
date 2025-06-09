#include "cache/ndc.h"

#include <cstdlib>  // For std::rand
#include <vector>

#include "mc.h"

uint64_t NDCScheme::access(MemReq& req) {
    // Determine request type
    ReqType type = (req.type == GETS || req.type == GETX) ? LOAD : STORE;
    // Address address = req.lineAddr % (_ext_size / 64);
    Address address = req.lineAddr;

    uint32_t mcdram_select = 0;
    Address mc_address = phyAddr2cacheAddr(address);
    uint64_t set_num = getSetNum(mc_address);
    // Address tag = getTag(mc_address);
    Address tag = address;

    _accessed_ext_lines_set.insert(address);
    _accessed_ext_lines = _accessed_ext_lines_set.size();
    _accessed_ext_pages_set.insert(address / (_page_size / 64));
    _accessed_ext_pages = _accessed_ext_pages_set.size();

    // info("phy_addr = 0x%lx, cache_addr = 0x%lx, set_num = %ld, tag = 0x%lx\n", address, mc_address, set_num, tag);

    // Check for cache hit
    uint32_t hit_way = _num_ways;
    for (uint32_t way = 0; way < _num_ways; way++) {
        if (_cache[set_num].ways[way].valid && _cache[set_num].ways[way].tag == tag) {
            hit_way = way;
            break;
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
        } else {
            // Cache miss: Fetch from main memory and fill the cache
            _num_miss_per_step++;
            _numLoadMiss.inc();

            // Fetch data from main memory
            MemReq main_memory_req = {address, GETS, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
            data_ready_cycle = _mc->_ext_dram->access(main_memory_req, 1, 4);
            _ext_bw_per_step += 4;

            // Fill cache
            // Victim selection: prefer invalid, then clean, then dirty (random among equals)
            std::vector<uint32_t> candidates;
            for (uint32_t way = 0; way < _num_ways; way++) {
                if (!_cache[set_num].ways[way].valid) {
                    candidates.push_back(way);
                }
            }
            if (candidates.empty()) {
                for (uint32_t way = 0; way < _num_ways; way++) {
                    if (_cache[set_num].ways[way].valid && !_cache[set_num].ways[way].dirty) {
                        candidates.push_back(way);
                    }
                }
            }
            if (candidates.empty()) {
                for (uint32_t way = 0; way < _num_ways; way++) {
                    if (_cache[set_num].ways[way].valid && _cache[set_num].ways[way].dirty) {
                        candidates.push_back(way);
                    }
                }
            }
            uint32_t victim_way = candidates[std::rand() % candidates.size()];

            // Handle eviction if victim is dirty
            if (_cache[set_num].ways[victim_way].valid && _cache[set_num].ways[victim_way].dirty) {
                // N.B. Load line from dram cache before write-back.
                Address victim_address = mc_address; // pseudo-address
                MemReq read_req = {victim_address, GETS, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
                _mc->_mcdram[mcdram_select]->access(read_req, 2, 4);
                _mc_bw_per_step += 4;

                Address wb_address = _cache[set_num].ways[victim_way].tag;
                MemReq wb_req = {wb_address, PUTX, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
                _mc->_ext_dram->access(wb_req, 2, 4);  // Write-back to main memory
                _ext_bw_per_step += 4;
                _numDirtyEviction.inc();
            } else if (_cache[set_num].ways[victim_way].valid) {
                _numCleanEviction.inc();
            }

            // Insert new line (fill operation)
            _cache[set_num].ways[victim_way].tag = tag;
            _cache[set_num].ways[victim_way].valid = true;
            _cache[set_num].ways[victim_way].dirty = false;  // LOAD: line is clean
            updateUtilizationStats(set_num, victim_way);
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
        } else {
            // Write miss
            _num_miss_per_step++;
            _numStoreMiss.inc();

            // Victim selection: prefer invalid, then clean, then dirty (random among equals)
            std::vector<uint32_t> candidates;
            for (uint32_t way = 0; way < _num_ways; way++) {
                if (!_cache[set_num].ways[way].valid) {
                    candidates.push_back(way);
                }
            }
            if (candidates.empty()) {
                for (uint32_t way = 0; way < _num_ways; way++) {
                    if (_cache[set_num].ways[way].valid && !_cache[set_num].ways[way].dirty) {
                        candidates.push_back(way);
                    }
                }
            }
            if (candidates.empty()) {
                for (uint32_t way = 0; way < _num_ways; way++) {
                    if (_cache[set_num].ways[way].valid && _cache[set_num].ways[way].dirty) {
                        candidates.push_back(way);
                    }
                }
            }
            uint32_t victim_way = candidates[std::rand() % candidates.size()];

            // Handle eviction if victim is dirty
            if (_cache[set_num].ways[victim_way].valid && _cache[set_num].ways[victim_way].dirty) {
                // N.B. Load line from dram cache before write-back.
                Address victim_address = mc_address; // pseudo-address
                MemReq read_req = {victim_address, GETS, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
                _mc->_mcdram[mcdram_select]->access(read_req, 2, 4);
                _mc_bw_per_step += 4;

                Address wb_address = _cache[set_num].ways[victim_way].tag;
                MemReq wb_req = {wb_address, PUTX, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
                _mc->_ext_dram->access(wb_req, 2, 4);  // Write-back to main memory, non-critical
                _ext_bw_per_step += 4;
                _numDirtyEviction.inc();
            } else if (_cache[set_num].ways[victim_way].valid) {
                _numCleanEviction.inc();
            }

            // Insert new line
            _cache[set_num].ways[victim_way].tag = tag;
            _cache[set_num].ways[victim_way].valid = true;
            _cache[set_num].ways[victim_way].dirty = true;  // STORE: mark as dirty
            data_ready_cycle = req.cycle;
            updateUtilizationStats(set_num, victim_way);
        }
    }

    return data_ready_cycle;
}

void NDCScheme::period(MemReq& req) {
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
}

void NDCScheme::initStats(AggregateStat* parentStat) {
    AggregateStat* stats = new AggregateStat();
    stats->init("ndcCache", "NDC Cache stats");
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