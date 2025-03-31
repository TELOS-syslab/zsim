#include "cache/unison.h"

#include "mc.h"

uint64_t UnisonCacheScheme::access(MemReq& req) {
    ReqType type = (req.type == GETS || req.type == GETX) ? LOAD : STORE;
	Address address = req.lineAddr % (_ext_size / 64);
	uint32_t mcdram_select = (address / 64) % _mc->_mcdram_per_mc;
	Address mc_address = (address / 64 / _mc->_mcdram_per_mc * 64) | (address % 64); 
	Address tag = address / (_granularity / 64);
    uint64_t set_num = tag % _num_sets;
    uint32_t hit_way = _num_ways;
    uint64_t data_ready_cycle = req.cycle;
    MESIState state;
    bool counter_access = false;

    // Check TLB for hit
    if (_tlb.find(tag) == _tlb.end()) {
        _tlb[tag] = TLBEntry{tag, _num_ways, 0, 0, 0};
    }

    if (_tlb[tag].way != _num_ways) {
        hit_way = _tlb[tag].way;
        assert(_cache[set_num].ways[hit_way].valid &&
               _cache[set_num].ways[hit_way].tag == tag);
    } else {
        for (uint32_t i = 0; i < _num_ways; i++)
            assert(_cache[set_num].ways[i].tag != tag || !_cache[set_num].ways[i].valid);  // @chunk: can TLB hold all tags? maybe it is actually the page table?
    }

    // Tag and data access
    if (type == LOAD) {
        req.lineAddr = mc_address;
        req.cycle = _mc->_mcdram[mcdram_select]->access(req, 0, 6);
        _mc_bw_per_step += 6;
        _numTagLoad.inc();
        req.lineAddr = address;
    } else {
        MemReq tag_probe = {mc_address, GETS, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
        req.cycle = _mc->_mcdram[mcdram_select]->access(tag_probe, 0, 2);
        _mc_bw_per_step += 2;
        _numTagLoad.inc();
    }

    if (hit_way != _num_ways) {
        // Cache hit
        _num_hit_per_step++;
        if (type == STORE) {
            MemReq write_req = {mc_address, PUTX, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
            req.cycle = _mc->_mcdram[mcdram_select]->access(write_req, 1, 4);
            _mc_bw_per_step += 4;
            _numStoreHit.inc();
        } else {
            _numLoadHit.inc();
        }
        data_ready_cycle = req.cycle;
        _page_placement_policy->handleCacheHit(tag, type, set_num, &_cache[set_num], counter_access, hit_way);

        // Update LRU information
        MemReq tag_update_req = {mc_address, PUTX, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
        _mc->_mcdram[mcdram_select]->access(tag_update_req, 2, 2);
        _mc_bw_per_step += 2;
        _numTagStore.inc();

        // Update touch/dirty bitvectors
        uint64_t bit = (address - tag * 64) / 4;
        assert(bit < 16 && bit >= 0);
        bit = ((uint64_t)1UL) << bit;
        _tlb[tag].touch_bitvec |= bit;
        if (type == STORE) {
            _tlb[tag].dirty_bitvec |= bit;
        }
    } else {
        // Cache miss
        _num_miss_per_step++;
        if (type == LOAD)
            _numLoadMiss.inc();
        else
            _numStoreMiss.inc();

        // Handle placement
        uint32_t replace_way = _page_placement_policy->handleCacheMiss(tag, type, set_num, &_cache[set_num], counter_access);

        if (type == LOAD) {
            req.cycle = _mc->_ext_dram->access(req, 1, 4);
            _ext_bw_per_step += 4;
        } else if (type == STORE && replace_way >= _num_ways) {
            req.cycle = _mc->_ext_dram->access(req, 1, 4);
            _ext_bw_per_step += 4;
        }
        data_ready_cycle = req.cycle;

        if (replace_way < _num_ways) {
            // Handle eviction if needed
            if (_cache[set_num].ways[replace_way].valid) {
                Address replaced_tag = _cache[set_num].ways[replace_way].tag;
                _tlb[replaced_tag].way = _num_ways;

                uint32_t dirty_lines = __builtin_popcountll(_tlb[replaced_tag].dirty_bitvec) * 4;
                uint32_t touch_lines = __builtin_popcountll(_tlb[replaced_tag].touch_bitvec) * 4;

                assert(touch_lines > 0 && touch_lines <= 64);
                assert(dirty_lines <= 64);

                _numTouchedLines.inc(touch_lines);
                _numEvictedLines.inc(dirty_lines);

                if (dirty_lines > 0) {
                    _numDirtyEviction.inc();
                    // Load dirty lines from MCDRAM
                    MemReq load_req = {mc_address, GETS, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
                    _mc->_mcdram[mcdram_select]->access(load_req, 2, dirty_lines * 4);
                    _mc_bw_per_step += dirty_lines * 4;

                    // Store dirty lines to ext DRAM
                    MemReq wb_req = {replaced_tag * 64, PUTX, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
                    _mc->_ext_dram->access(wb_req, 2, dirty_lines * 4);
                    _ext_bw_per_step += dirty_lines * 4;
                } else {
                    _numCleanEviction.inc();
                }
            }

            // Load new page from ext DRAM
            MemReq load_req = {tag * 64, GETS, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
            _mc->_ext_dram->access(load_req, 2, _footprint_size * 4);
            _ext_bw_per_step += _footprint_size * 4;

            // Store new page to MCDRAM
            MemReq insert_req = {mc_address, PUTX, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
            _mc->_mcdram[mcdram_select]->access(insert_req, 2, _footprint_size * 4);
            if (!_sram_tag) {
                _mc->_mcdram[mcdram_select]->access(insert_req, 2, 2);  // store tag
                _mc_bw_per_step += 2;
            }
            _mc_bw_per_step += _footprint_size * 4;
            _numTagStore.inc();
            _numPlacement.inc();

            // Update cache entry
            _cache[set_num].ways[replace_way].valid = true;
            _cache[set_num].ways[replace_way].tag = tag;
            _cache[set_num].ways[replace_way].dirty = (type == STORE);
            _tlb[tag].way = replace_way;

            // Initialize bitvectors
            uint64_t bit = (address - tag * 64) / 4;
            assert(bit < 16 && bit >= 0);
            bit = ((uint64_t)1UL) << bit;
            _tlb[tag].touch_bitvec = bit;
            _tlb[tag].dirty_bitvec = (type == STORE) ? bit : 0;
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

void UnisonCacheScheme::period(MemReq& req) {
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

void UnisonCacheScheme::initStats(AggregateStat* parentStat) {
    AggregateStat* stats = new AggregateStat();
    stats->init("unisonCache", "UnisonCache stats");
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
    _numTouchedLines.init("totalTouchLines", "Total # of touched lines");
    stats->append(&_numTouchedLines);
    _numEvictedLines.init("totalEvictLines", "Total # of evicted lines");
    stats->append(&_numEvictedLines);
    _numCounterAccess.init("counterAccess", "Counter Access");
    stats->append(&_numCounterAccess);
    parentStat->append(stats);
}