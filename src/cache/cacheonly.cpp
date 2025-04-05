#include "cache/cacheonly.h"

#include "mc.h"

uint64_t CacheOnlyScheme::access(MemReq& req) {
    // Address address = req.lineAddr % (_ext_size / 64);
    Address address = req.lineAddr;
    uint32_t mcdram_select = (address / 64) % _mc->_mcdram_per_mc;
    Address mc_address = (address / 64 / _mc->_mcdram_per_mc * 64) | (address % 64);

    req.lineAddr = mc_address;
    req.cycle = _mc->_mcdram[mcdram_select]->access(req, 0, 4);
    req.lineAddr = address;
    _numLoadHit.inc();

    return req.cycle;
}

void CacheOnlyScheme::period(MemReq& req) {
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

void CacheOnlyScheme::initStats(AggregateStat* parentStat) {
    AggregateStat* stats = new AggregateStat();
    stats->init("cacheOnly", "CacheOnly stats");
    _numLoadHit.init("loadHit", "Load Hit");
    stats->append(&_numLoadHit);
    stats->append(_numTotalLines);
    stats->append(_numAccessedLines);
    stats->append(_numReaccessedLines);
    parentStat->append(stats);
}