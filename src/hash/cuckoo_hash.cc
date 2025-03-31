#include "cuckoo_hash.h"

#include <queue>

#include <cassert>

void CuckooHash::InitCuckooBuckets(void)
{
    for (auto& single_bucket: buckets_) {
        for (auto& bucket_entry: single_bucket) {
            bucket_entry.valid = false;
            bucket_entry.footprint = 0;
            bucket_entry.map_idx = 0;
            bucket_entry.way_idx = 0;
        }
    }
}

void CuckooHash::InitIndexMetadata(void)
{
    index_metadata_.reserve(nr_bucket_ * hash_assoc_);
}

uint64_t CuckooHash::Hash(uint64_t key, uint64_t map_idx, uint64_t map_unit_idx_in_group)
{
    assert(map_idx < nr_hash_);

    if (map_idx == 0) {
        return XXHash(key) % nr_set_per_page_;
    } else if (map_idx == 1) {
        return CityHash(key) % nr_set_per_page_;
    } else {
        assert(0);
        return -1;
    }
}

uint64_t CuckooHash::GetHashAssocLimit(void)
{
    return cur_hash_assoc_limit_;
}

uint64_t CuckooHash::GetSingleBucketCapacity(void)
{
    return nr_bucket_;
}

uint64_t CuckooHash::GetCapacity(void)
{
    return hash_assoc_ * nr_bucket_;
}

bool CuckooHash::CheckFreeBucket(uint64_t bucket_set_idx, uint64_t& target_hash_assoc, uint64_t& target_way_idx)
{
    assert(cur_hash_assoc_limit_ <= hash_assoc_ &&
        cur_hash_assoc_limit_ <= buckets_.size());
    assert(target_hash_assoc < cur_hash_assoc_limit_);
    assert(bucket_set_idx < nr_set_per_page_);

    for (uint64_t hash_assoc_idx = 0; hash_assoc_idx < cur_hash_assoc_limit_; hash_assoc_idx += 1) {
        for (uint64_t way_idx = 0; way_idx < bucket_assoc_; way_idx += 1) {
            uint64_t bucket_idx = bucket_set_idx * bucket_assoc_ + way_idx;
            assert(bucket_idx < buckets_[hash_assoc_idx].size());

            if (!buckets_[hash_assoc_idx][bucket_idx].valid) {
                target_hash_assoc = hash_assoc_idx;
                target_way_idx = way_idx;
                return true;
            }
        }
    }

    target_hash_assoc = 0;
    target_way_idx = 0;
    return false;
}

void CuckooHash::ClearCuckooHashInfo(CuckooPathEntry cuckoo_path_entry, uint64_t map_unit_idx_in_group)
{
    // clear cuckoo hash构建过程中的元数据
    uint64_t origin_hash_idx = 1 - cuckoo_path_entry.target_hash_idx;
    assert(origin_hash_idx < nr_hash_);
    uint64_t bucket_idx = Hash(cuckoo_path_entry.phy_cache_addr, origin_hash_idx, map_unit_idx_in_group) * bucket_assoc_ + 
        cuckoo_path_entry.origin_dram_way_idx;

    assert(cuckoo_path_entry.origin_hash_assoc_idx < buckets_.size());
    assert(bucket_idx < buckets_[cuckoo_path_entry.origin_hash_assoc_idx].size());
    CuckooEntry cuckoo_entry = buckets_[cuckoo_path_entry.origin_hash_assoc_idx][bucket_idx];
    assert(cuckoo_entry.valid &&
        cuckoo_entry.footprint == cuckoo_path_entry.phy_cache_addr);

    buckets_[cuckoo_path_entry.origin_hash_assoc_idx][bucket_idx] = CuckooEntry {
        .valid = false,
        .map_idx = 0,
        .way_idx = 0,
        .footprint = 0
    };

    // clear index info
    index_metadata_.erase(cuckoo_path_entry.phy_cache_addr);
}

void CuckooHash::_UpdateCuckooHashInfo(uint64_t phy_cache_addr, uint64_t hash_idx, uint64_t target_hash_assoc_idx, uint64_t target_way_idx,
    uint64_t map_unit_idx_in_group)
{
    uint64_t bucket_idx = Hash(phy_cache_addr, hash_idx, map_unit_idx_in_group) * bucket_assoc_ + target_way_idx;
    assert(target_hash_assoc_idx < buckets_.size());
    assert(target_way_idx < bucket_assoc_);
    assert(bucket_idx < buckets_[target_hash_assoc_idx].size());
    //assert(!buckets_[target_hash_assoc_idx][bucket_idx].valid);

    buckets_[target_hash_assoc_idx][bucket_idx] = CuckooEntry {
        .valid = true,
        .map_idx = (uint8_t)hash_idx,
        .way_idx = (uint8_t)target_way_idx,
        .footprint = phy_cache_addr
    };
}

void CuckooHash::_UpdateIndexInfo(uint64_t phy_cache_addr, uint64_t hash_idx, uint64_t target_hash_assoc_idx, uint64_t target_way_idx)
{
    assert(index_metadata_.find(phy_cache_addr) == index_metadata_.end());
    assert(hash_idx < nr_hash_);
    index_metadata_[phy_cache_addr] = IndexMetadata{.is_cuckoo = true, 
        .map_idx = (uint8_t)hash_idx, .hash_assoc_idx = (uint8_t)target_hash_assoc_idx, 
        .way_idx = (uint8_t)target_way_idx};
}

void CuckooHash::UpdateInfo(uint64_t phy_cache_addr, uint64_t hash_idx, uint64_t target_hash_assoc_idx, uint64_t target_way_idx,
    uint64_t map_unit_idx_in_group)
{
    _UpdateCuckooHashInfo(phy_cache_addr, hash_idx, target_hash_assoc_idx, target_way_idx, map_unit_idx_in_group);
    _UpdateIndexInfo(phy_cache_addr, hash_idx, target_hash_assoc_idx, target_way_idx);
}

void CuckooHash::UpdateDirectMapInfo(uint64_t phy_cache_addr)
{
    assert(index_metadata_.find(phy_cache_addr) == index_metadata_.end());
    index_metadata_[phy_cache_addr] = IndexMetadata{.is_cuckoo = false,
        .map_idx = (uint8_t)-1, .hash_assoc_idx = (uint8_t)-1, .way_idx = (uint8_t)-1
        };
}

void CuckooHash::_InsertBFSQueue(uint64_t phy_cache_addr, uint64_t mapped_hash_idx,
    uint64_t hash_assoc_idx, uint64_t way_idx, uint64_t pre_tree_idx,
    std::queue<CuckooSetBFSEntry>& bucket_set_bfs_queue, 
    std::vector<CuckooPathTreeNode>& cuckoo_path_tree,
    std::vector<bool>& is_traverse,
    uint64_t map_unit_idx_in_group)
{
    for (uint64_t hash_idx = 0; hash_idx < nr_hash_; hash_idx += 1) {
        if (hash_idx == mapped_hash_idx) {
            continue;
        }

        uint64_t set_idx = Hash(phy_cache_addr, hash_idx, map_unit_idx_in_group);
        // 判断是否被插入过，并且设置is_traverse
        assert(set_idx < is_traverse.size());
        if (is_traverse[set_idx]) {
            continue;
        }
        is_traverse[set_idx] = 1;

        // 断言没有被映射的bucket中，都不含有phy_cache_addr
        for (uint64_t hash_assoc_idx = 0; hash_assoc_idx < cur_hash_assoc_limit_; hash_assoc_idx += 1) {
            for (uint64_t way_idx = 0; way_idx < bucket_assoc_; way_idx += 1) {
                uint64_t bucket_idx = set_idx * bucket_assoc_ + way_idx;
                assert(hash_assoc_idx < buckets_.size());
                assert(bucket_idx < buckets_[hash_assoc_idx].size());
                //assert(buckets_[hash_assoc_idx][bucket_idx].valid);
                //assert(buckets_[hash_assoc_idx][bucket_idx].footprint != phy_cache_addr);
            }
        }

        // 将新的set插入BFS queue
        cuckoo_path_tree.emplace_back(CuckooPathTreeNode {
            .pre_tree_idx = pre_tree_idx,
            .phy_cache_addr = phy_cache_addr,
            .hash_assoc_idx = hash_assoc_idx,
            .hash_idx = hash_idx,   // 相当于target
            .way_idx = way_idx
        });
        uint64_t tree_idx = cuckoo_path_tree.size() - 1;
        bucket_set_bfs_queue.push(CuckooSetBFSEntry{.bucket_set_idx = set_idx, .tree_idx = tree_idx});
    }
}

void CuckooHash::_CaculateCuckooPath(uint64_t free_tree_idx, uint64_t free_hash_assoc_idx, 
    uint64_t free_way_idx, const std::vector<CuckooPathTreeNode>& cuckoo_path_tree,
    std::vector<CuckooPathEntry>& cuckoo_path)
{
    uint64_t tree_idx = free_tree_idx;
    uint64_t target_hash_assoc_idx = free_hash_assoc_idx;
    uint64_t target_way_idx = free_way_idx;
    while(true) {
        if (tree_idx == (uint64_t)-1) {
            // 代表此时第一个tree_node已经处理完了
            return;
        }

        assert(tree_idx < cuckoo_path_tree.size());
        CuckooPathTreeNode tree_node = cuckoo_path_tree[tree_idx];
        
        cuckoo_path.insert(cuckoo_path.begin(), 
            CuckooPathEntry{
                .phy_cache_addr = tree_node.phy_cache_addr,

                .origin_hash_idx = (1 - tree_node.hash_idx),
                .origin_hash_assoc_idx = tree_node.hash_assoc_idx,
                .origin_dram_way_idx = tree_node.way_idx,

                .target_hash_idx = tree_node.hash_idx,
                .target_hash_assoc_idx = target_hash_assoc_idx,
                .target_bucket_way_idx = target_way_idx
            });

        tree_idx = tree_node.pre_tree_idx;
        target_hash_assoc_idx = tree_node.hash_assoc_idx;
        target_way_idx = tree_node.way_idx;
    }
}

bool CuckooHash::GetCuckooPath(uint64_t phy_cache_addr, std::vector<CuckooPathEntry>& cuckoo_path,
    uint64_t map_unit_idx_in_group)
{
    std::queue<CuckooSetBFSEntry> bucket_set_bfs_queue;
    std::vector<CuckooPathTreeNode> cuckoo_path_tree;   // 存储着导致set插入的phy_cache_addr信息
    std::vector<bool> is_traverse(nr_set_per_page_, 0);

    // 初始化bucket_set_bfs_queue
    _InsertBFSQueue(phy_cache_addr, nr_hash_, (uint64_t)-1, (uint64_t)-1, (uint64_t)-1, 
        bucket_set_bfs_queue, cuckoo_path_tree, is_traverse, map_unit_idx_in_group);

    // 当BFS Queue为空或者找到路径时，退出循环
    bool is_found = false;
    uint64_t free_hash_assoc = 0;
    uint64_t free_way_idx = 0;
    while(!bucket_set_bfs_queue.empty() && !is_found) {
        CuckooSetBFSEntry set_bfs_entry = bucket_set_bfs_queue.front();
        bucket_set_bfs_queue.pop();

        // 判断该bucket_set内是否有空闲位置
        if (CheckFreeBucket(set_bfs_entry.bucket_set_idx, free_hash_assoc, free_way_idx)) {
            is_found = true;
            _CaculateCuckooPath(set_bfs_entry.tree_idx,
                free_hash_assoc, free_way_idx, cuckoo_path_tree, cuckoo_path);

            break;
        }
        
        // 如果没有空闲位置，则将该bucket_set中所有entry对应的bucket_set插入BFS queue
        for (uint64_t way_idx = 0; way_idx < bucket_assoc_; way_idx += 1) {
            uint64_t bucket_idx = set_bfs_entry.bucket_set_idx * bucket_assoc_ + way_idx;
            uint64_t hash_assoc_idx = cur_hash_assoc_limit_ - 1;
            assert(hash_assoc_idx < buckets_.size());
            assert(bucket_idx < buckets_[hash_assoc_idx].size());
            assert(buckets_[hash_assoc_idx][bucket_idx].valid);

            CuckooEntry cuckoo_entry = buckets_[hash_assoc_idx][bucket_idx];
            _InsertBFSQueue(cuckoo_entry.footprint, cuckoo_entry.map_idx, hash_assoc_idx, 
                cuckoo_entry.way_idx, set_bfs_entry.tree_idx, bucket_set_bfs_queue, 
                cuckoo_path_tree, is_traverse, map_unit_idx_in_group);
        }
        
    }

    if (is_found) {
        
        return true;
    } else {
        cuckoo_path.clear();

        return false;
    }
}

void CuckooHash::KickOutCuckooPath(const std::vector<CuckooPathEntry>& cuckoo_path, 
    uint64_t map_unit_idx_in_group)
{
    assert(cuckoo_path.size() > 0);
    for (auto it = cuckoo_path.rbegin(); it != cuckoo_path.rend(); it += 1) {
        ClearCuckooHashInfo(*it, map_unit_idx_in_group);
        UpdateInfo(it->phy_cache_addr, it->target_hash_idx, it->target_hash_assoc_idx, 
            it->target_bucket_way_idx, map_unit_idx_in_group);
    }
}

void CuckooHash::_UpdateCuckooPathDramSet(std::vector<CuckooPathEntry>& cuckoo_path, 
    uint64_t map_unit_idx_in_group)
{
    for (uint64_t idx = 0; idx < cuckoo_path.size(); idx += 1) {
        uint64_t set_idx_in_page = Hash(cuckoo_path[idx].phy_cache_addr, cuckoo_path[idx].target_hash_idx, map_unit_idx_in_group);
        cuckoo_path[idx].target_dram_set_idx = begin_dram_set_idx_ + set_idx_in_page;
    }
}

bool CuckooHash::Insert(uint64_t phy_cache_addr, std::vector<CuckooPathEntry>& cuckoo_path, 
    uint64_t map_unit_idx_in_group)
{
    bool insert_success = false;

    assert(cuckoo_metric_.nr_total_entry_ <= nr_bucket_ * hash_assoc_);

    if (GetCuckooPath(phy_cache_addr, cuckoo_path, map_unit_idx_in_group)) {
        insert_success = true;
        
        // 取出第一个元素，特殊处理
        CuckooPathEntry non_kick_entry = cuckoo_path[0];
        cuckoo_path.erase(cuckoo_path.begin());

        // 除了第一个元素，剩下的元素Kick out,更新Cuckoo Hash和Index Metadata
        if (cuckoo_path.size() > 0) {
            cuckoo_metric_.nr_kick_out_ += 1;
            cuckoo_metric_.cum_cuckoo_path_len_ += cuckoo_path.size();
            
            KickOutCuckooPath(cuckoo_path, map_unit_idx_in_group);

            _UpdateCuckooPathDramSet(cuckoo_path, map_unit_idx_in_group);
        }

        // 新元素对应的cuckoo hash和index信息更新
        UpdateInfo(phy_cache_addr, non_kick_entry.target_hash_idx, non_kick_entry.target_hash_assoc_idx,
            non_kick_entry.target_bucket_way_idx, map_unit_idx_in_group);

    }

    cuckoo_metric_.nr_total_entry_ += 1;
    if (insert_success) {
        cuckoo_metric_.nr_cuckoo_map_ += 1;
        
        assert(cuckoo_metric_.nr_cuckoo_map_ <= hash_assoc_ * nr_bucket_);
        if (cuckoo_metric_.nr_cuckoo_map_ > cur_hash_assoc_limit_ * nr_bucket_ * target_load_ratio_ / 100) {
            cur_hash_assoc_limit_ = (cur_hash_assoc_limit_ + 1) <= hash_assoc_? cur_hash_assoc_limit_ + 1: hash_assoc_;
        }

        return true;
    } else {
        UpdateDirectMapInfo(phy_cache_addr);
        cuckoo_metric_.nr_direct_map_ += 1;

        return false;
    }
}

uint64_t CuckooHash::_GetTargetSetIdxDefault(uint64_t phy_cache_addr, uint64_t map_unit_idx_in_group)
{
    return (phy_cache_addr / bucket_assoc_) % nr_set_per_page_;
}

uint64_t CuckooHash::_GetTargetSetIdx(uint64_t phy_cache_addr, uint64_t map_unit_idx_in_group)
{
    // 元素已经插入过，直接检索即可
    assert(index_metadata_.find(phy_cache_addr) != index_metadata_.end());

    IndexMetadata index_info = index_metadata_.find(phy_cache_addr)->second;
    if (index_info.is_cuckoo) {
        uint64_t target_set_idx_in_page = Hash(phy_cache_addr, index_info.map_idx, map_unit_idx_in_group);
        uint64_t target_cache_addr_in_page = target_set_idx_in_page * bucket_assoc_ + index_info.way_idx;
        assert(index_info.map_idx < nr_hash_);
        assert(index_info.hash_assoc_idx < hash_assoc_);
        assert(index_info.hash_assoc_idx < cur_hash_assoc_limit_);
        assert(index_info.way_idx < bucket_assoc_);
        assert(target_cache_addr_in_page < buckets_[index_info.hash_assoc_idx].size());
        assert(buckets_[index_info.hash_assoc_idx][target_cache_addr_in_page].valid);
        assert(buckets_[index_info.hash_assoc_idx][target_cache_addr_in_page].map_idx == index_info.map_idx);
        assert(buckets_[index_info.hash_assoc_idx][target_cache_addr_in_page].footprint == phy_cache_addr);

        return begin_dram_set_idx_ + Hash(phy_cache_addr, index_info.map_idx, map_unit_idx_in_group);
    } else {
        uint64_t set_idx_in_page = _GetTargetSetIdxDefault(phy_cache_addr, map_unit_idx_in_group);

        return begin_dram_set_idx_ + set_idx_in_page;
    }
}

uint64_t CuckooHash::GetTargetSetIdx(uint64_t phy_cache_addr, std::vector<CuckooPathEntry>& cuckoo_path,
    uint64_t map_unit_idx_in_group)
{
    if (index_metadata_.find(phy_cache_addr) != index_metadata_.end()) {
        return _GetTargetSetIdx(phy_cache_addr, map_unit_idx_in_group);
    } else {
        // 插入新元素
        Insert(phy_cache_addr, cuckoo_path, map_unit_idx_in_group);
        return _GetTargetSetIdx(phy_cache_addr, map_unit_idx_in_group);
    }
}

uint64_t CuckooHash::GetTargetSetIdx(uint64_t phy_cache_addr, uint64_t map_unit_idx_in_group)
{
    std::vector<CuckooPathEntry> cuckoo_path;
    return GetTargetSetIdx(phy_cache_addr, cuckoo_path, map_unit_idx_in_group);
}

void CuckooHash::RemoveEntry(uint64_t hp_addr, uint64_t map_unit_idx_in_group)
{
    for (uint64_t idx = 0; idx < 32768; idx += 1) {
        uint64_t phy_cache_addr = hp_addr * 32768 + idx;
        if (index_metadata_.find(phy_cache_addr) != index_metadata_.end()) {
            IndexMetadata index_info = index_metadata_.find(phy_cache_addr)->second;

            // 删除index info
            index_metadata_.erase(phy_cache_addr);
            
            if (!index_info.is_cuckoo) {
                continue;
            }

            // 删除cuckoo_hash用于构建的元数据
            uint64_t bucket_idx = Hash(phy_cache_addr, index_info.map_idx, map_unit_idx_in_group);
            assert(index_info.hash_assoc_idx < buckets_.size());
            assert(bucket_idx < buckets_[index_info.hash_assoc_idx].size());
            CuckooEntry cuckoo_entry = buckets_[index_info.hash_assoc_idx][bucket_idx];
            assert(cuckoo_entry.valid &&
                cuckoo_entry.footprint == phy_cache_addr);

            buckets_[index_info.hash_assoc_idx][bucket_idx] = CuckooEntry {
                .valid = false,
                .map_idx = 0,
                .way_idx = 0,
                .footprint = 0
            };
            // buckets_[index_info.hash_assoc_idx][bucket_idx] = CuckooEntry();
            // buckets_[index_info.hash_assoc_idx][bucket_idx].valid = false;
            // buckets_[index_info.hash_assoc_idx][bucket_idx].map_idx = 0;
            // buckets_[index_info.hash_assoc_idx][bucket_idx].way_idx = 0;
            // buckets_[index_info.hash_assoc_idx][bucket_idx].footprint = 0;
        }
    }
}