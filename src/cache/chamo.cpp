

#include "cache/chamo.h"

#include <cstdlib>  // For std::rand
#include <vector>

#include "mc.h"

uint64_t CHAMOScheme::_GetBaseRank(uint64_t dram_cache_idx, uint64_t target_level_idx)
{
    // assert(target_level_idx < dram_ratio_);

    // assert(target_level_idx < dram_base_rank_.size());
    // assert(dram_cache_idx < dram_base_rank_[target_level_idx].size());
    // uint64_t base_rank = dram_base_rank_[target_level_idx][dram_cache_idx];

    // 先计算基础的base_rank
    uint64_t base_rank = 1;
    uint64_t cxl_level;
    for (cxl_level = 0; cxl_level < dram_ratio_; cxl_level += 1) {
        assert(cxl_level < access_bit_map_.size());

        if (cxl_level == target_level_idx) {
            assert(access_bit_map_[cxl_level][dram_cache_idx]);
            break;
        }

        if (access_bit_map_[cxl_level][dram_cache_idx]) {
            base_rank += 1;
        }
    }

    assert(cxl_level < dram_ratio_);
    assert(base_rank <= dram_ratio_);

    assert(base_rank > 0);
    assert(base_rank <= dram_ratio_);
    assert(_GetColCap(dram_cache_idx) >= base_rank);

    return base_rank;
}

uint64_t CHAMOScheme::_GetColCap(uint64_t dram_cache_idx)
{
    uint64_t cap = 0;
    for (uint64_t level_idx = 0; level_idx < dram_ratio_; level_idx += 1) {
        assert(level_idx < access_bit_map_.size());
        assert(dram_cache_idx < access_bit_map_[level_idx].size());

        if (access_bit_map_[level_idx][dram_cache_idx]) {
            cap += 1;
        }
    }

    return cap;
}

bool CHAMOScheme::CheckCuckooPath(uint64_t dram_cache_idx, uint64_t& cuckoo_path_len)
{
    cuckoo_path_len = 0;

    // 判断cuckoo_path上是否已经存在超过tolerate的情况
    // 判断是否仍然有cuckoo_path上的col仍然保证overflow + cap < nr_map_limit_
    for (uint64_t idx = 0; idx < cuckoo_window_len_; idx += 1) {
        uint64_t col_idx = (dram_cache_idx + idx) % nr_dram_cache_;
        assert(col_idx < dram_overflow_rank_.size());

        // 已经有col无法容忍新的元素了，退出循环
        if (_GetColCap(col_idx) + dram_overflow_rank_[col_idx] >= 2 * nr_map_limit_) {
            break;
        }

        if (_GetColCap(col_idx) + dram_overflow_rank_[col_idx] < nr_map_limit_) {
            cuckoo_path_len = idx;
            return true;
        }
    }

    return false;
}

void CHAMOScheme::UpdateCUckooPath(uint64_t dram_cache_idx, uint64_t cuckoo_path_len)
{
    for (uint64_t idx = 1; idx <= cuckoo_path_len; idx += 1) {
        uint64_t col_idx = (dram_cache_idx + idx) % nr_dram_cache_;
        assert(col_idx < dram_overflow_rank_.size());

        dram_overflow_rank_[col_idx] += 1;
        assert(dram_overflow_rank_[col_idx] <= nr_map_limit_);
    }
}

uint64_t CHAMOScheme::_GetOverflowRank(uint64_t dram_cache_idx, uint64_t target_level_idx)
{
    assert(dram_cache_idx < dram_overflow_rank_.size());
    uint64_t overflow_rank = dram_overflow_rank_[dram_cache_idx];
    assert(overflow_rank <= nr_map_limit_);
    return overflow_rank;
}

uint64_t CHAMOScheme::_GetSelfContainRank(uint64_t dram_cache_idx)
{
    assert(dram_cache_idx < dram_self_contain_rank_.size());
    assert(dram_self_contain_rank_[dram_cache_idx] <= nr_map_limit_);
    return dram_self_contain_rank_[dram_cache_idx];   
}

void CHAMOScheme::_UpdateMapLimit(void)
{
    // 更新nr_map_limit_
    nr_map_limit_ = (((hash_metric_.nr_cuckoo_cnt_ * 100 / load_ratio_) + nr_dram_cache_ - 1) / nr_dram_cache_);
    assert(nr_map_limit_ <= dram_ratio_ + 1);

    nr_map_limit_ = std::min(dram_ratio_, nr_map_limit_);
    nr_map_limit_ = std::max(1UL, nr_map_limit_);
    assert(nr_map_limit_ < dram_ratio_);
}

uint64_t CHAMOScheme::_HashIdxToAddr(uint64_t line_addr_in_level, uint64_t level_idx, uint64_t hash_idx)
{
    uint64_t target_addr = 0;
    assert(level_idx < is_cuckoo_hash_.size());
    assert(line_addr_in_level < is_cuckoo_hash_[level_idx].size());
    if (hash_idx == 0 || hash_idx == 1) {
        // 使用cuckoo hash
        //assert(is_cuckoo_hash_[level_idx][line_addr_in_level]);

        // 更新cuckoo成功映射的函数 (这里主要是更新nr_map_limit更新后没计入的cuckoo_cnt)
        if (!is_cuckoo_hash_[level_idx][line_addr_in_level]) {
            is_cuckoo_hash_[level_idx][line_addr_in_level] = true;

            hash_metric_.nr_cuckoo_cnt_ += 1;
        }

        target_addr = next_line_.Hash(line_addr_in_level, hash_idx);

    } else {
        // 使用默认映射策略
        assert(hash_idx == 2);

        // 更新cuckoo映射失败的函数
        if (is_cuckoo_hash_[level_idx][line_addr_in_level]) {
            is_cuckoo_hash_[level_idx][line_addr_in_level] = false;

            hash_metric_.nr_cuckoo_cnt_ -= 1;
        }

        assert(line_addr_in_level + level_idx * nr_dram_cache_ < nr_cxl_cache_);
        target_addr = XXHash(line_addr_in_level + level_idx * nr_dram_cache_) % nr_dram_cache_;
    }

    assert(hash_metric_.nr_cuckoo_cnt_ <= hash_metric_.nr_touched_cnt_);

    // 更新hash_idx变化情况
    assert(level_idx < hash_idx_.size());
    assert(line_addr_in_level < hash_idx_[level_idx].size());
    if (hash_idx_[level_idx][line_addr_in_level] != hash_idx) {
        hash_metric_.nr_period_hash_change_cnt_ += 1;

        hash_idx_[level_idx][line_addr_in_level] = hash_idx;
    }

    return target_addr;
}

uint64_t CHAMOScheme::_RankToAddr(uint64_t base_rank, uint64_t overflow_rank,
    uint64_t self_contain_rank, uint64_t line_addr_in_level, uint64_t level_idx)
{
    uint8_t target_hash_idx = (uint8_t)-1;
    uint64_t target_addr = 0;
    uint64_t next_col_idx = (line_addr_in_level + 1) % nr_dram_cache_;
    assert(level_idx < is_cuckoo_hash_.size());
    assert(line_addr_in_level < is_cuckoo_hash_[level_idx].size());
    assert(line_addr_in_level < dram_self_contain_rank_.size());
    assert(line_addr_in_level < dram_overflow_rank_.size());
    assert(next_col_idx < dram_self_contain_rank_.size());
    assert(next_col_idx < dram_overflow_rank_.size());

    if (base_rank <= dram_overflow_rank_[next_col_idx]) {
        // 能够映射到隔壁列
        target_hash_idx = 1;
        assert(base_rank <= nr_map_limit_);
    } else if (base_rank - dram_overflow_rank_[next_col_idx] <= dram_self_contain_rank_[line_addr_in_level]) {
        // 能够映射到当前列
        target_hash_idx = 0;
        assert(base_rank - dram_overflow_rank_[next_col_idx] <= dram_self_contain_rank_[line_addr_in_level]);
        assert((base_rank - dram_overflow_rank_[next_col_idx]) + dram_overflow_rank_[line_addr_in_level]<= nr_map_limit_);
    } else {
        // 无法映射
        target_hash_idx = 2;
    }

    // 计算映射函数
    assert(target_hash_idx != (uint8_t)-1);
    target_addr = _HashIdxToAddr(line_addr_in_level, level_idx, target_hash_idx);

    return target_addr;
}

// target_rank_idx: 1->n
uint64_t CHAMOScheme::CalculateRankToAddr(uint64_t dram_cache_idx, uint64_t target_level_idx)
{
    // 更新nr_map_limit_
    _UpdateMapLimit();

    uint64_t base_rank = _GetBaseRank(dram_cache_idx, target_level_idx);
    uint64_t overflow_rank = _GetOverflowRank(dram_cache_idx, target_level_idx);
    uint64_t sefl_contain_rank = _GetSelfContainRank(dram_cache_idx);

    // base_rank代表当前列的基础访问cache block个数
    // overflow_work代表又左边cache block列"传递"的映射迁移
    return _RankToAddr(base_rank, overflow_rank, sefl_contain_rank, dram_cache_idx, target_level_idx);
}

uint64_t CHAMOScheme::GetAlterCxlLineAddr(uint64_t phy_cache_addr)
{   
    uint64_t shuffle_line_addr = lcg_.LCG_hash(phy_cache_addr, 0);
    assert(shuffle_line_addr < nr_cxl_cache_);

    return shuffle_line_addr;

    //return phy_cache_addr;
}

void CHAMOScheme::UpdateMappingInfo(uint64_t dram_cache_idx, uint64_t level_idx)
{
    assert(level_idx < is_cuckoo_hash_.size());
    assert(dram_cache_idx < is_cuckoo_hash_[level_idx].size());
    assert(!is_cuckoo_hash_[level_idx][dram_cache_idx]);

    // 先判断能否抢夺右侧列的资源
    uint64_t next_col_idx = (dram_cache_idx + 1) % nr_dram_cache_;
    assert(dram_cache_idx < dram_self_contain_rank_.size());
    assert(dram_cache_idx < dram_overflow_rank_.size());
    assert(level_idx < is_cuckoo_hash_.size());
    assert(dram_cache_idx < is_cuckoo_hash_[level_idx].size());
    assert(next_col_idx < dram_overflow_rank_.size());
    assert(next_col_idx < dram_self_contain_rank_.size());
    if (dram_self_contain_rank_[next_col_idx] + dram_overflow_rank_[next_col_idx] < nr_map_limit_) {
        dram_overflow_rank_[next_col_idx] += 1;
        assert(dram_self_contain_rank_[next_col_idx] + dram_overflow_rank_[next_col_idx] <= nr_map_limit_);

        is_cuckoo_hash_[level_idx][dram_cache_idx] = true;
        hash_metric_.nr_cuckoo_cnt_ += 1;

        return;
    }

    // 再判断是否能够插入到自己的当前对应的列中
    if (dram_self_contain_rank_[dram_cache_idx] + dram_overflow_rank_[dram_cache_idx] < nr_map_limit_) {
        dram_self_contain_rank_[dram_cache_idx] += 1;
        assert(dram_self_contain_rank_[dram_cache_idx] + dram_overflow_rank_[dram_cache_idx] <= nr_map_limit_);

        is_cuckoo_hash_[level_idx][dram_cache_idx] = true;
        hash_metric_.nr_cuckoo_cnt_ += 1;

        return;
    } 

    // 无法插入
    return;
}

uint64_t CHAMOScheme::Index(uint64_t cache_addr)
{

    uint64_t phy_line_addr = lcg_.LCG_hash(cache_addr, 0);
    assert(phy_line_addr < nr_cxl_cache_);

    hash_metric_.nr_period_access_cnt_ += 1;

    // 我们提出cxl_level的概念, e.g., DRAM:CXL = 1:4情况下，CXL_level = 4
    uint64_t cxl_level = phy_line_addr / nr_dram_cache_;
    assert(cxl_level < access_bit_map_.size());

    uint64_t line_offset_in_level = phy_line_addr % nr_dram_cache_;
    assert(line_offset_in_level < access_bit_map_[cxl_level].size());

    if (!access_bit_map_[cxl_level][line_offset_in_level]) {
        assert(cxl_level < dram_base_rank_.size());
        assert(line_offset_in_level < dram_base_rank_[cxl_level].size());
        assert(dram_base_rank_[cxl_level][line_offset_in_level] == 0);
        dram_base_rank_[cxl_level][line_offset_in_level] = _GetColCap(line_offset_in_level) + 1;
        assert(dram_base_rank_[cxl_level][line_offset_in_level] > 0 && 
            dram_base_rank_[cxl_level][line_offset_in_level] <= dram_ratio_);

        access_bit_map_[cxl_level][line_offset_in_level] = true;
        hash_metric_.nr_touched_cnt_ += 1;
        hash_metric_.nr_period_newly_cache_cnt_ += 1;

        assert(dram_base_rank_[cxl_level][line_offset_in_level] == _GetColCap(line_offset_in_level));

        UpdateMappingInfo(line_offset_in_level, cxl_level);

    }

    uint64_t target_dram_line_addr = CalculateRankToAddr(line_offset_in_level, cxl_level);

    return target_dram_line_addr;
}

uint64_t CHAMOScheme::access(MemReq& req) {
    // Determine request type
    ReqType type = (req.type == GETS || req.type == GETX) ? LOAD : STORE;
    Address address = req.lineAddr % (_ext_size / 64);

    uint32_t mcdram_select = 0;
    Address mc_address = Index(address);
    uint64_t set_num = Index(address);
    Address tag = address;
    assert(mc_address < _cache_size / 64);
    assert(address < _ext_size / 64);

    // info("phy_addr = 0x%lx, cache_addr = 0x%lx, set_num = %ld, tag = 0x%lx, line_num = %ld\n", address, mc_address, set_num, tag, line_num);

    // Check for cache hit
    uint32_t hit_way = 0;
    if (!(_cache[set_num].ways[hit_way].valid && _cache[set_num].ways[hit_way].tag == tag)) {
        hit_way = _num_ways;
    }

    uint64_t data_ready_cycle = 0;
    MESIState state;

    uint64_t victim_way = 0;

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


            // Handle eviction if victim is dirty
            if (_cache[set_num].ways[victim_way].valid && _cache[set_num].ways[victim_way].dirty) {
                Address wb_address = _cache[set_num].ways[victim_way].tag * _granularity;
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

            // Handle eviction if victim is dirty
            if (_cache[set_num].ways[victim_way].valid && _cache[set_num].ways[victim_way].dirty) {
                Address wb_address = _cache[set_num].ways[victim_way].tag * _granularity;
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
        }
    }

    return data_ready_cycle;
}

void CHAMOScheme::period(MemReq& req) {
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

void CHAMOScheme::initStats(AggregateStat* parentStat) {
    AggregateStat* stats = new AggregateStat();
    stats->init("chamoCache", "CHAMO Cache stats");
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