#ifndef ZSIM_HASH_CUCKOO_HASH_H_
#define ZSIM_HASH_CUCKOO_HASH_H_

#include <queue>
#include <vector>
#include <unordered_map>

#include <cassert>
#include <cstdint>

#include "hash.h"

class CuckooHash {
    public:
        struct CuckooMetric {
            uint64_t nr_cuckoo_map_; // number of cache block using cuckoo map
            uint64_t nr_direct_map_; // number of cache block using direct map
            uint64_t nr_total_entry_;

            uint64_t nr_kick_out_; // 发生过多少次kickout, kickout完整的cuckoo path算一次
            uint64_t cum_cuckoo_path_len_;
        };

        struct CuckooPathEntry {
            uint64_t phy_cache_addr = 0;

            uint64_t origin_hash_idx = 0;
            uint64_t origin_hash_assoc_idx = 0;
            uint64_t origin_dram_way_idx = 0;

            uint64_t target_hash_idx = 0;
            uint64_t target_hash_assoc_idx = 0;
            uint64_t target_bucket_way_idx = 0;

            uint64_t target_dram_set_idx = 0;
        };

        CuckooMetric cuckoo_metric_;

        CuckooHash()
        {
            CuckooHash(0, 0, 64, 0, 0, 0);
        };
        CuckooHash(uint64_t dram_cache_addr, uint64_t page_size, uint64_t cache_size, 
            uint64_t nr_bucket, uint64_t hash_assoc, uint64_t target_load_ratio)
        {
            // FIXME 调用该构造函数会出现非常奇怪的现象
            CuckooHash(dram_cache_addr, page_size, cache_size, nr_bucket, hash_assoc, 1, target_load_ratio);
        }
        CuckooHash(uint64_t dram_set_idx, uint64_t page_size, uint64_t cache_size, 
            uint64_t nr_bucket, uint64_t hash_assoc, uint64_t bucket_assoc, uint64_t target_load_ratio):
            cuckoo_metric_{.nr_cuckoo_map_ = 0, .nr_direct_map_ = 0, 
                .nr_total_entry_ = 0, .nr_kick_out_ = 0, .cum_cuckoo_path_len_ = 0},
            begin_dram_set_idx_(dram_set_idx), page_size_(page_size), nr_set_per_page_(page_size_ / (cache_size * bucket_assoc)),
            nr_bucket_(nr_bucket), hash_assoc_(hash_assoc), bucket_assoc_(bucket_assoc), cur_hash_assoc_limit_(1), nr_hash_(2), 
            target_load_ratio_(target_load_ratio), buckets_(hash_assoc, std::vector<CuckooEntry>(nr_bucket))
            {
                assert(nr_set_per_page_ * bucket_assoc_ == nr_bucket_);
                assert(page_size_ % (cache_size * bucket_assoc) == 0);
                InitCuckooBuckets();
                InitIndexMetadata();
            };
        uint64_t GetHashAssocLimit(void);
        uint64_t GetSingleBucketCapacity(void);
        uint64_t GetCapacity(void);
        //uint64_t GetTargetCacheAddr(uint64_t phy_cache_addr);
        //uint64_t GetTargetCacheAddr(uint64_t phy_cache_addr, std::vector<CuckooPathEntry>& cuckoo_path);
        uint64_t GetTargetSetIdx(uint64_t phy_cache_addr, uint64_t map_unit_idx_in_group = 0);
        uint64_t GetTargetSetIdx(uint64_t phy_cache_addr, std::vector<CuckooPathEntry>& cuckoo_path,
            uint64_t map_unit_idx_in_group = 0);

        void RemoveEntry(uint64_t hp_addr, uint64_t map_unit_idx_in_group = 0);

    protected:
        struct IndexMetadata {
            bool is_cuckoo = 0;
            uint8_t map_idx = 0;
            uint8_t hash_assoc_idx = 0;
            uint8_t way_idx = 0;
        };

        struct CuckooEntry {
            bool valid = false;
            uint8_t map_idx = 0; // the idx of mapping functions
            uint8_t way_idx = 0;
            uint64_t footprint = 0; // the phy_cache_addr is footprint
        };

        struct CuckooSetBFSEntry {
            uint64_t bucket_set_idx = 0;
            uint64_t tree_idx = 0;  // 导致插入该BFS Entry对应的phy_cache_addr的tree_idx
        };

        struct CuckooPathTreeNode {
            uint64_t pre_tree_idx = 0;
            uint64_t phy_cache_addr = 0;
            uint64_t hash_assoc_idx = 0;
            uint64_t hash_idx = 0;
            uint64_t way_idx = 0;
        };
        
        uint64_t begin_dram_set_idx_;
        uint64_t page_size_;
        uint64_t nr_set_per_page_;
        uint64_t nr_bucket_;
        uint64_t hash_assoc_;
        uint64_t bucket_assoc_;
        uint64_t cur_hash_assoc_limit_;
        uint64_t nr_hash_;  // hard code: 2
        uint64_t target_load_ratio_;

        std::vector<std::vector<CuckooEntry>> buckets_; // used to construct cuckoo hash，以cache为单位，不是以set为单位
        std::unordered_map<uint64_t, IndexMetadata> index_metadata_; // phy_cache_addr -> index_info

        void InitCuckooBuckets(void);
        void InitIndexMetadata(void);
        virtual uint64_t Hash(uint64_t key, uint64_t map_idx, uint64_t map_unit_idx_in_group);

        void _UpdateCuckooPathDramSet(std::vector<CuckooPathEntry>& cuckoo_path, uint64_t map_unit_idx_in_group);
        bool Insert(uint64_t phy_cache_addr, std::vector<CuckooPathEntry>& cuckoo_path,
            uint64_t map_unit_idx_in_group);

        bool CheckFreeBucket(uint64_t bucket_set_idx, uint64_t& target_hash_assoc, uint64_t& target_way_idx);

        void ClearCuckooHashInfo(CuckooPathEntry cuckoo_path_entry, uint64_t map_unit_idx_in_group);
        void _UpdateCuckooHashInfo(uint64_t phy_cache_addr, uint64_t hash_idx, uint64_t target_hash_assoc_idx, uint64_t target_way_idx,
            uint64_t map_unit_idx_in_group);
        void _UpdateIndexInfo(uint64_t phy_cache_idx, uint64_t hash_idx, uint64_t target_hash_assoc_idx, uint64_t target_way_idx);
        void UpdateInfo(uint64_t phy_cache_addr, uint64_t hash_idx, uint64_t target_hash_assoc_idx, uint64_t target_way_idx,
            uint64_t map_unit_idx_in_group);
        void UpdateDirectMapInfo(uint64_t phy_cache_addr);

        void KickOutCuckooPath(const std::vector<CuckooPathEntry>& cuckoo_path, uint64_t map_unit_idx_in_group);
        
        // BFS related function
        void _InsertBFSQueue(uint64_t phy_cache_addr, uint64_t mapped_hash_idx, 
            uint64_t hash_assoc_idx, uint64_t way_idx, uint64_t pre_tree_idx,
            std::queue<CuckooSetBFSEntry>& bucket_set_bfs_queue,
            std::vector<CuckooPathTreeNode>& cuckoo_path_tree,
            std::vector<bool>& is_traverse, uint64_t map_unit_idx_in_group);   // 将该cache block所对应的所有bucket压入BFS queue中
        void _CaculateCuckooPath(uint64_t free_tree_idx, uint64_t free_hash_assoc_idx, 
            uint64_t free_way_idx, const std::vector<CuckooPathTreeNode>& cuckoo_path_tree,
            std::vector<CuckooPathEntry>& cuckoo_path);  // 利用树信息计算cuckoo_path
        bool GetCuckooPath(uint64_t phy_cache_addr, std::vector<CuckooPathEntry>& cuckoo_path,
            uint64_t map_unit_idx_in_group);

        //bool GetTargetBucketIdx(uint64_t footprint, uint64_t& way_idx);  // 在Set中查找对应的元素
        //uint64_t _GetTargetCacheAddr(uint64_t phy_cache_addr);  // 已经确认此时cache block已经被插入过
        uint64_t _GetTargetSetIdx(uint64_t phy_cache_addr, uint64_t map_unit_idx_in_group);  // 已经确认此时cache block已经被插入过
        virtual uint64_t _GetTargetSetIdxDefault(uint64_t phy_cache_addr, uint64_t map_unit_idx_in_group);
};

#endif