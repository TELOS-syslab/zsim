#include "cache/banshee.h"

#include "mc.h"

uint64_t BansheeCacheScheme::access(MemReq& req) {
    ReqType type = (req.type == GETS || req.type == GETX) ? LOAD : STORE;
    Address address = req.lineAddr;
    uint32_t mcdram_select = (address / 64) % _mc->_mcdram_per_mc;
    Address mc_address = (address / 64 / _mc->_mcdram_per_mc * 64) | (address % 64);
    Address tag = address / _granularity;
    uint64_t set_num = tag % _num_sets;
    uint32_t hit_way = _num_ways;
    uint64_t data_ready_cycle = req.cycle;
    MESIState state;
    bool hybrid_tag_probe = false;
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

    // Check if we need to probe tag
    if (type == STORE) {
        if (_tag_buffer->existInTB(tag) == _tag_buffer->getNumWays() && set_num >= _ds_index) {
            _numTBDirtyMiss.inc();
            if (!_sram_tag) hybrid_tag_probe = true;
        } else {
            _numTBDirtyHit.inc();
        }
    }

    if (_sram_tag) req.cycle += _llc_latency;

    if (hit_way != _num_ways) {
        // Cache hit
        _num_hit_per_step++;
        _page_placement_policy->handleCacheHit(tag, type, set_num, &_cache[set_num], counter_access, hit_way);
        if (type == STORE) {
            _cache[set_num].ways[hit_way].dirty = true;
            _numStoreHit.inc();
        } else {
            _numLoadHit.inc();
        }

        if (!hybrid_tag_probe) {
            req.lineAddr = mc_address;
            req.cycle = _mc->_mcdram[mcdram_select]->access(req, 0, 4);
            _mc_bw_per_step += 4;
            req.lineAddr = address;
            data_ready_cycle = req.cycle;
            if (type == LOAD && _tag_buffer->canInsert(tag)) {
                _tag_buffer->insert(tag, false);
            }
        } else {
            assert(!_sram_tag);
            MemReq tag_probe = {mc_address, GETS, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
            req.cycle = _mc->_mcdram[mcdram_select]->access(tag_probe, 0, 2);
            _mc_bw_per_step += 2;
            _numTagLoad.inc();
            req.lineAddr = mc_address;
            req.cycle = _mc->_mcdram[mcdram_select]->access(req, 1, 4);
            _mc_bw_per_step += 4;
            req.lineAddr = address;
            data_ready_cycle = req.cycle;
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

        if (hybrid_tag_probe) {
            MemReq tag_probe = {mc_address, GETS, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
            req.cycle = _mc->_mcdram[mcdram_select]->access(tag_probe, 0, 2);
            _mc_bw_per_step += 2;
            req.cycle = _mc->_ext_dram->access(req, 1, 4);
            _ext_bw_per_step += 4;
            _numTagLoad.inc();
            data_ready_cycle = req.cycle;
        } else {
            req.cycle = _mc->_ext_dram->access(req, 0, 4);
            _ext_bw_per_step += 4;
            data_ready_cycle = req.cycle;
        }

        if (replace_way < _num_ways) {
            // Handle eviction
            if (_cache[set_num].ways[replace_way].valid) {
                Address replaced_tag = _cache[set_num].ways[replace_way].tag;
                _tlb[replaced_tag].way = _num_ways;

                if (_cache[set_num].ways[replace_way].dirty) {
                    _numDirtyEviction.inc();
                    // Load page from MCDRAM
                    MemReq load_req = {mc_address, GETS, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
                    _mc->_mcdram[mcdram_select]->access(load_req, 2, (_granularity / 64) * 4);
                    _mc_bw_per_step += (_granularity / 64) * 4;
                    // Store to ext DRAM
                    MemReq wb_req = {replaced_tag * 64, PUTX, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
                    _mc->_ext_dram->access(wb_req, 2, (_granularity / 64) * 4);
                    _ext_bw_per_step += (_granularity / 64) * 4;
                } else {
                    _numCleanEviction.inc();
                }

                // Update tag buffer
                if (!_tag_buffer->canInsert(tag, replaced_tag)) {
                    _tag_buffer->clearTagBuffer();
                    _tag_buffer->setClearTime(req.cycle);
                    _numTagBufferFlush.inc();
                }
                assert(_tag_buffer->canInsert(tag, replaced_tag));
                _tag_buffer->insert(tag, true);
                _tag_buffer->insert(replaced_tag, true);
            }

            // Load new page from ext DRAM
            MemReq load_req = {tag * 64, GETS, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
            _mc->_ext_dram->access(load_req, 2, (_granularity / 64) * 4);
            _ext_bw_per_step += (_granularity / 64) * 4;

            // Store to MCDRAM
            MemReq insert_req = {mc_address, PUTX, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
            _mc->_mcdram[mcdram_select]->access(insert_req, 2, (_granularity / 64) * 4);
            if (!_sram_tag) {
                _mc->_mcdram[mcdram_select]->access(insert_req, 2, 2);
                _mc_bw_per_step += 2;
            }
            _mc_bw_per_step += (_granularity / 64) * 4;
            _numTagStore.inc();
            _numPlacement.inc();

            // Update cache entry
            _cache[set_num].ways[replace_way].valid = true;
            _cache[set_num].ways[replace_way].tag = tag;
            _cache[set_num].ways[replace_way].dirty = (type == STORE);
            _tlb[tag].way = replace_way;
        } else if (type == LOAD && _tag_buffer->canInsert(tag)) {
            _tag_buffer->insert(tag, false);
        }
    }

    // TODO. make this part work again.
    if (counter_access && !_sram_tag) {
        // TODO may not need the counter load if we can store freq info inside TAD
        /////// model counter access in mcdram
        // One counter read and one coutner write
        assert(set_num >= _ds_index);
        _numCounterAccess.inc();
        MemReq counter_req = {mc_address, GETS, req.childId, &state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
        _mc->_mcdram[mcdram_select]->access(counter_req, 2, 2);
        counter_req.type = PUTX;
        _mc->_mcdram[mcdram_select]->access(counter_req, 2, 2);
        _mc_bw_per_step += 4;
    }

    // Check tag buffer occupancy
    if (_tag_buffer->getOccupancy() > 0.7) {
        printf("[Tag Buffer FLUSH] occupancy = %f\n", _tag_buffer->getOccupancy());
        _tag_buffer->clearTagBuffer();
        _tag_buffer->setClearTime(req.cycle);
        _numTagBufferFlush.inc();
    }

    // Handle bandwidth balance if needed
    if (_num_requests % _step_length == 0) {
        period(req);
    }

    return data_ready_cycle;
}

void BansheeCacheScheme::period(MemReq& req) {
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

                        if (_scheme == BansheeCache && meta.valid) {
                            _tlb[meta.tag].way = _num_ways;
                            if (!_tag_buffer->canInsert(meta.tag)) {
                                printf("Rebalance. [Tag Buffer FLUSH] occupancy = %f\n", _tag_buffer->getOccupancy());
                                _tag_buffer->clearTagBuffer();
                                _tag_buffer->setClearTime(req.cycle);
                                _numTagBufferFlush.inc();
                            }
                            assert(_tag_buffer->canInsert(meta.tag));
                            _tag_buffer->insert(meta.tag, true);
                        }

                        meta.valid = false;
                        meta.dirty = false;
                    }
                    if (_scheme == BansheeCache) {
                        _page_placement_policy->flushChunk(set);
                    }
                }
            }
        }
        _ds_index = ((int64_t)_ds_index + delta_index <= 0) ? 0 : _ds_index + delta_index;
        printf("_ds_index = %ld/%ld\n", _ds_index, _num_sets);
    }
}

void BansheeCacheScheme::initStats(AggregateStat* parentStat) {
    AggregateStat* stats = new AggregateStat();
    stats->init("bansheeCache", "BansheeCache stats");
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
    _numTagBufferFlush.init("tagBufferFlush", "Number of tag buffer flushes");
    stats->append(&_numTagBufferFlush);
    _numTBDirtyHit.init("TBDirtyHit", "Tag buffer hits (LLC dirty evict)");
    stats->append(&_numTBDirtyHit);
    _numTBDirtyMiss.init("TBDirtyMiss", "Tag buffer misses (LLC dirty evict)");
    stats->append(&_numTBDirtyMiss);
    _numCounterAccess.init("counterAccess", "Counter Access");
    stats->append(&_numCounterAccess);
    parentStat->append(stats);
}

TagBuffer::TagBuffer(Config& config) {
    uint32_t tb_size = config.get<uint32_t>("sys.mem.mcdram.tag_buffer_size", 1024);
    _num_ways = 8;
    _num_sets = tb_size / _num_ways;
    _entry_occupied = 0;
    _tag_buffer = (TagBufferEntry**)gm_malloc(sizeof(TagBufferEntry*) * _num_sets);
    //_tag_buffer = new TagBufferEntry * [_num_sets];
    for (uint32_t i = 0; i < _num_sets; i++) {
        _tag_buffer[i] = (TagBufferEntry*)gm_malloc(sizeof(TagBufferEntry) * _num_ways);
        //_tag_buffer[i] = new TagBufferEntry [_num_ways];
        for (uint32_t j = 0; j < _num_ways; j++) {
            _tag_buffer[i][j].remap = false;
            _tag_buffer[i][j].tag = 0;
            _tag_buffer[i][j].lru = j;
        }
    }
}

uint32_t TagBuffer::existInTB(Address tag) {
    uint32_t set_num = tag % _num_sets;
    for (uint32_t i = 0; i < _num_ways; i++)
        if (_tag_buffer[set_num][i].tag == tag) {
            // printf("existInTB\n");
            return i;
        }
    return _num_ways;
}

bool TagBuffer::canInsert(Address tag) {
#if 1
    uint32_t num = 0;
    for (uint32_t i = 0; i < _num_sets; i++)
        for (uint32_t j = 0; j < _num_ways; j++)
            if (_tag_buffer[i][j].remap)
                num++;
    assert(num == _entry_occupied);
#endif

    uint32_t set_num = tag % _num_sets;
    // printf("tag_buffer=%#lx, set_num=%d, tag_buffer[set_num]=%#lx, num_ways=%d\n",
    //	(uint64_t)_tag_buffer, set_num, (uint64_t)_tag_buffer[set_num], _num_ways);
    for (uint32_t i = 0; i < _num_ways; i++)
        if (!_tag_buffer[set_num][i].remap || _tag_buffer[set_num][i].tag == tag)
            return true;
    return false;
}

bool TagBuffer::canInsert(Address tag1, Address tag2) {
    uint32_t set_num1 = tag1 % _num_sets;
    uint32_t set_num2 = tag2 % _num_sets;
    if (set_num1 != set_num2)
        return canInsert(tag1) && canInsert(tag2);
    else {
        uint32_t num = 0;
        for (uint32_t i = 0; i < _num_ways; i++)
            if (!_tag_buffer[set_num1][i].remap || _tag_buffer[set_num1][i].tag == tag1 || _tag_buffer[set_num1][i].tag == tag2)
                num++;
        return num >= 2;
    }
}

void TagBuffer::insert(Address tag, bool remap) {
    uint32_t set_num = tag % _num_sets;
    uint32_t exist_way = existInTB(tag);
#if 1
    for (uint32_t i = 0; i < _num_ways; i++)
        for (uint32_t j = i + 1; j < _num_ways; j++) {
            // if (_tag_buffer[set_num][i].tag != 0 && _tag_buffer[set_num][i].tag == _tag_buffer[set_num][j].tag) {
            //	for (uint32_t k = 0; k < _num_ways; k++)
            //		printf("_tag_buffer[%d][%d]: tag=%ld, remap=%d\n",
            //			set_num, k, _tag_buffer[set_num][k].tag, _tag_buffer[set_num][k].remap);
            // }
            assert(_tag_buffer[set_num][i].tag != _tag_buffer[set_num][j].tag || _tag_buffer[set_num][i].tag == 0);
        }
#endif
    if (exist_way < _num_ways) {
        // the tag already exists in the Tag Buffer
        assert(tag == _tag_buffer[set_num][exist_way].tag);
        if (remap) {
            if (!_tag_buffer[set_num][exist_way].remap)
                _entry_occupied++;
            _tag_buffer[set_num][exist_way].remap = true;
        } else if (!_tag_buffer[set_num][exist_way].remap)
            updateLRU(set_num, exist_way);
        return;
    }

    uint32_t max_lru = 0;
    uint32_t replace_way = _num_ways;
    for (uint32_t i = 0; i < _num_ways; i++) {
        if (!_tag_buffer[set_num][i].remap && _tag_buffer[set_num][i].lru >= max_lru) {
            max_lru = _tag_buffer[set_num][i].lru;
            replace_way = i;
        }
    }
    assert(replace_way != _num_ways);
    _tag_buffer[set_num][replace_way].tag = tag;
    _tag_buffer[set_num][replace_way].remap = remap;
    if (!remap) {
        // printf("\tset=%d way=%d, insert. no remap\n", set_num, replace_way);
        updateLRU(set_num, replace_way);
    } else {
        // printf("set=%d way=%d, insert\n", set_num, replace_way);
        _entry_occupied++;
    }
}

void TagBuffer::updateLRU(uint32_t set_num, uint32_t way) {
    assert(!_tag_buffer[set_num][way].remap);
    for (uint32_t i = 0; i < _num_ways; i++)
        if (!_tag_buffer[set_num][i].remap && _tag_buffer[set_num][i].lru < _tag_buffer[set_num][way].lru)
            _tag_buffer[set_num][i].lru++;
    _tag_buffer[set_num][way].lru = 0;
}

void TagBuffer::clearTagBuffer() {
    _entry_occupied = 0;
    for (uint32_t i = 0; i < _num_sets; i++) {
        for (uint32_t j = 0; j < _num_ways; j++) {
            _tag_buffer[i][j].remap = false;
            _tag_buffer[i][j].tag = 0;
            _tag_buffer[i][j].lru = j;
        }
    }
}
