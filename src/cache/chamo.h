#ifndef _CHAMO_SCHEME_H_
#define _CHAMO_SCHEME_H_

#include <cmath>  // For log2
#include <string>

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <list>
#include <queue>
#include <set>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cache/cache_scheme.h"
#include "cache/hash/hash.h"
#include "mc.h"
#include "stats.h"

class CHAMOScheme : public CacheScheme  {
    public:
        struct BitMapHashMetric {
            uint64_t nr_touched_cnt_;    // 实际访问的cache block
            uint64_t nr_cuckoo_cnt_;    // 成功应用cuckoo hash的cache block个数
            
            // 下面两个属性需要周期性更新
            uint64_t nr_period_hash_change_cnt_;   // 当前周期内cuckoo hash idx发生变化的数量
            uint64_t nr_period_access_cnt_; // 当前周期内被访问的个数
            uint64_t nr_period_newly_cache_cnt_;    //当前周期内被新访问的cache个数
        };

    public: 
        CHAMOScheme(Config& config, MemoryController* mc): CacheScheme(config, mc),
            nr_dram_cache_(_cache_size / 64),
            nr_cxl_cache_(_ext_size / 64),
            dram_ratio_(nr_cxl_cache_ / nr_dram_cache_ ),
            nr_map_limit_(1),
            load_ratio_(95),
            cuckoo_window_len_(4),
            hash_metric_{0, 0, 0, 0, 0},
            dram_overflow_rank_(nr_dram_cache_, 0),
            dram_base_rank_(dram_ratio_, std::vector<uint64_t>(nr_dram_cache_, 0)),
            dram_self_contain_rank_(nr_dram_cache_, 0),
            access_bit_map_(dram_ratio_, std::vector<bool>(nr_dram_cache_, false)),
            is_cuckoo_hash_(dram_ratio_, std::vector<bool>(nr_dram_cache_, false)),
            hash_idx_(dram_ratio_, std::vector<uint8_t>(nr_dram_cache_, (uint8_t)-1)),
            lcg_(nr_cxl_cache_),
            next_line_(nr_dram_cache_)
            {
                _scheme = CHAMO;
            };

    protected:

        uint64_t GetAlterCxlLineAddr(uint64_t phy_cache_addr);
        uint64_t access(MemReq& req) override;
        uint64_t Index(uint64_t phy_line_addr);

        void period(MemReq& req) override;
        void initStats(AggregateStat* parentStat) override;

    private:
        uint64_t nr_dram_cache_;
        uint64_t nr_cxl_cache_;
        uint64_t dram_ratio_;
        uint64_t nr_map_limit_; // 1->n
        uint64_t load_ratio_;
        uint64_t cuckoo_window_len_;
        BitMapHashMetric hash_metric_;
        std::ofstream cuckoo_metric_stream_;

        std::vector<uint64_t> dram_overflow_rank_;
        std::vector<std::vector<uint64_t>> dram_base_rank_;
        std::vector<uint64_t> dram_self_contain_rank_;    // 映射到自己对应的这一列，和overflow_rank相互抢同一列的资源
        std::vector<std::vector<bool>> access_bit_map_;
        std::vector<std::vector<bool>> is_cuckoo_hash_;
        std::vector<std::vector<uint8_t>> hash_idx_;    // -1代表默认值
        LCGHash lcg_;
        NextLineHash next_line_;

        // Statistics counters
        Counter _numCleanEviction;
        Counter _numDirtyEviction;
        Counter _numLoadHit;
        Counter _numLoadMiss;
        Counter _numStoreHit;
        Counter _numStoreMiss;

        // CheckCuckooPath
        // 判断在cuckoo_path length内是否能成功插入,cuckoo_path_len为成功插入的路径长度:0->cuckoo_window_len-1
        bool CheckCuckooPath(uint64_t dram_cache_idx, uint64_t& cuckoo_path_len);  
        void UpdateCUckooPath(uint64_t dram_cache_idx, uint64_t cuckoo_path_len);

        // 计算每个cache block的等效rank
        uint64_t _GetColCap(uint64_t dram_cache_idx);   // 获取某列中被访问的cache block个数  
        uint64_t _GetBaseRank(uint64_t dram_cache_idx, uint64_t target_level_idx);
        uint64_t _GetOverflowRank(uint64_t dram_cache_idx, uint64_t target_level_idx);
        uint64_t _GetSelfContainRank(uint64_t dram_cache_idx);

        // 利用rank计算地址
        void _UpdateMapLimit(void); // 基于nr_cuckoo更新map_limit
        uint64_t _HashIdxToAddr(uint64_t line_addr_in_leve, uint64_t level_idx, uint64_t hash_idx); // 利用hash函数idx计算地址
        uint64_t _RankToAddr(uint64_t base_rank, uint64_t overflow_rank,
            uint64_t self_contain_rank, uint64_t line_addr_in_level, uint64_t level_idx);   // rank: 1->n，利用rank计算对应的addr
        uint64_t CalculateRankToAddr(uint64_t dram_cache_idx, 
            uint64_t target_level_idx);    // target_rank = base_rank +++ overflow_rank
        void UpdateMappingInfo(uint64_t dram_cache_idx, uint64_t level_idx);    // 更新映射信息，包括seft_contain以及抢旁边列的overflow，仅在插入新元素时更新
};

#endif