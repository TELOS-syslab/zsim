#include "cache/alloy.h"

#include "mc.h"

uint64_t AlloyCacheScheme::access(MemReq& req) {
    ReqType type = (req.type == GETS || req.type == GETX) ? LOAD : STORE;
	// // Address address = req.lineAddr % (_ext_size / 64);
    Address address = req.lineAddr;
	uint32_t mcdram_select = (address / 64) % _mc->_mcdram_per_mc;
	Address mc_address = (address / 64 / _mc->_mcdram_per_mc * 64) | (address % 64); 
	Address tag = address / (_granularity / 64);
    uint64_t set_num = tag % _num_sets;
    uint32_t hit_way = _num_ways;
    uint64_t data_ready_cycle = req.cycle;
    MESIState state;
    bool counter_access = false;

    // info("access: address = %ld, mc_address = %ld, tag = %ld, set_num = %ld", address, mc_address, tag, set_num);
    // Check for hit
    if (_cache[set_num].ways[0].valid &&
        _cache[set_num].ways[0].tag == tag &&
        set_num >= _ds_index) {
        hit_way = 0;
        // info("!!!!!hit:  _cache[set_num].ways[0].tag = %ld",  _cache[set_num].ways[0].tag);
    }

    // Handle tag access for loads
    if (type == LOAD && set_num >= _ds_index) {
        if (_sram_tag) {
            req.cycle += _llc_latency;
        } else {
            req.lineAddr = mc_address;
            req.cycle = _mc->_mcdram[mcdram_select]->access(req, 0, 6);
            _mc_bw_per_step += 6;
            _numTagLoad.inc();
            req.lineAddr = address;
        }
    }

    if (hit_way != _num_ways) {
        // Cache hit
        _num_hit_per_step++;
        if (type == LOAD && _sram_tag) {
            MemReq read_req = {mc_address, GETX, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
            req.cycle = _mc->_mcdram[mcdram_select]->access(read_req, 0, 4);
            _mc_bw_per_step += 4;
        }
        if (type == STORE) {
            MemReq write_req = {mc_address, PUTX, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
            req.cycle = _mc->_mcdram[mcdram_select]->access(write_req, 0, 4);
            _mc_bw_per_step += 4;
            _cache[set_num].ways[hit_way].dirty = true;
            _numStoreHit.inc();
        } else {
            _numLoadHit.inc();
        }
        data_ready_cycle = req.cycle;
    } else {
        // Cache miss
        _num_miss_per_step++;
        if (type == LOAD)
            _numLoadMiss.inc();
        else
            _numStoreMiss.inc();

        // Handle placement
        uint32_t replace_way = _num_ways;
        bool place = false;
        if (set_num >= _ds_index) {
            place = _line_placement_policy->handleCacheMiss(&_cache[set_num].ways[0]);
        }
        replace_way = place ? 0 : 1;

        // Handle data access
        if (type == LOAD) {
            if (!_sram_tag && set_num >= _ds_index) {
                req.cycle = _mc->_ext_dram->access(req, 1, 4);
            } else {
                req.cycle = _mc->_ext_dram->access(req, 0, 4);
            }
            _ext_bw_per_step += 4;
            data_ready_cycle = req.cycle;
        } else if (type == STORE && replace_way >= _num_ways) {
            req.cycle = _mc->_ext_dram->access(req, 0, 4);
            _ext_bw_per_step += 4;
            data_ready_cycle = req.cycle;
        } else if (type == STORE) {
            MemReq load_req = {address, GETS, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
            req.cycle = _mc->_ext_dram->access(load_req, 0, 4);
            _ext_bw_per_step += 4;
            data_ready_cycle = req.cycle;
        }

        // Handle replacement
        if (replace_way < _num_ways) {
            MemReq insert_req = {mc_address, PUTX, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
            uint32_t size = _sram_tag ? 4 : 6;
            _mc->_mcdram[mcdram_select]->access(insert_req, 2, size);
            _mc_bw_per_step += size;
            _numTagStore.inc();
            _numPlacement.inc();

            if (_cache[set_num].ways[replace_way].valid) {
                if (_cache[set_num].ways[replace_way].dirty) {
                    _numDirtyEviction.inc();
                    if (type == STORE && _sram_tag) {
                        MemReq load_req = {mc_address, GETS, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
                        req.cycle = _mc->_mcdram[mcdram_select]->access(load_req, 2, 4);
                        _mc_bw_per_step += 4;
                    }
                    MemReq wb_req = {_cache[set_num].ways[replace_way].tag, PUTX, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
                    _mc->_ext_dram->access(wb_req, 2, 4);
                    _ext_bw_per_step += 4;
                } else {
                    _numCleanEviction.inc();
                }
            }
            _cache[set_num].ways[replace_way].valid = true;
            _cache[set_num].ways[replace_way].tag = tag;
            _cache[set_num].ways[replace_way].dirty = (req.type == PUTX);
        }
    }

    if (counter_access && !_sram_tag) {
        _numCounterAccess.inc();
        MemReq counter_req = {mc_address, GETS, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
        _mc->_mcdram[mcdram_select]->access(counter_req, 2, 2);
        counter_req.type = PUTX;
        _mc->_mcdram[mcdram_select]->access(counter_req, 2, 2);
        _mc_bw_per_step += 4;
    }

    return data_ready_cycle;
}

void AlloyCacheScheme::period(MemReq& req) {
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

void AlloyCacheScheme::initStats(AggregateStat* parentStat) {
    AggregateStat* stats = new AggregateStat();
    stats->init("alloyCache", "AlloyCache stats");
    _numPlacement.init("placement", "Number of Placement");
    stats->append(&_numPlacement);
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
    _numTagLoad.init("tagLoad", "Number of tag loads");
    stats->append(&_numTagLoad);
    _numTagStore.init("tagStore", "Number of tag stores");
    stats->append(&_numTagStore);
    _numCounterAccess.init("counterAccess", "Counter Access");
    stats->append(&_numCounterAccess);
    parentStat->append(stats);
}