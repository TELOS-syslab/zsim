#include "cache/ndc.h"

#include <cstdint>

#include "cache_scheme.h"  // For CacheScheme base class
#include "g_std/g_vector.h"
#include "galloc.h"  // For GlobAlloc
#include "mc.h"
#include "memory_hierarchy.h"  // For Address type
#include "stats.h"             // For AggregateStat

uint64_t NDCScheme::access(MemReq& req) {
    uint64_t latency = 0;

    // Extract cache index and tag from address
    uint64_t index = (req.lineAddr / _granularity) % _num_sets;
    uint64_t tag = req.lineAddr / (_granularity * _num_sets);

    // Determine bank and row buffer state
    uint32_t bank = index % _num_banks;
    bool row_hit = (_open_rows[bank] == index);
    if (!row_hit) {
        if (_open_rows[bank] != static_cast<uint64_t>(-1)) {
            latency += _tRP;  // Precharge previous row
        }
        latency += _tRCD;  // Activate new row
        _open_rows[bank] = index;
    }

    // Tag matching
    bool hit = false;
    uint32_t way = 0;
    for (way = 0; way < _num_ways; way++) {
        if (_cache[index].ways[way].valid && _cache[index].ways[way].tag == tag) {
            hit = true;
            break;
        }
    }

    if (req.type == GETS || req.type == GETX) {  // Read
        if (hit) {
            latency += _deltaCL + _tCL;
            _num_hit_per_step++;
        } else {
            // Fetch from main memory
            // Address ext_addr = req.lineAddr;
            MemReq ext_req = req;
            latency += _mc->_ext_dram->access(ext_req);
            _num_miss_per_step++;

            // Allocate in cache
            uint32_t victim_way = selectVictim(index);
            if (_cache[index].ways[victim_way].valid && _cache[index].ways[victim_way].dirty) {
                // Handle eviction using victim buffer
                if (_victim_buffer->reserveSlot()) {
                    Address victim_addr = _cache[index].ways[victim_way].tag * (_granularity * _num_sets) + index * _granularity;
                    _victim_buffer->addEntry(victim_addr, index, victim_way);
                    _victim_buffer_entries++;
                } else {
                    // Victim buffer full, writeback directly
                    Address victim_addr = _cache[index].ways[victim_way].tag * (_granularity * _num_sets) + index * _granularity;
                    MemReq wb_req = {victim_addr, PUTX, req.childId, nullptr, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
                    latency += _mc->_ext_dram->access(wb_req);
                }
            }
            _cache[index].ways[victim_way].tag = tag;
            _cache[index].ways[victim_way].valid = true;
            _cache[index].ways[victim_way].dirty = false;
        }
    } else {  // Write
        if (hit) {
            latency += _deltaCL + _tCWL;
            _cache[index].ways[way].dirty = true;
            _num_hit_per_step++;
        } else {
            // Read-Modify-Write for write miss
            // First fetch the line
            Address ext_addr = req.lineAddr;
            MemReq ext_req = {ext_addr, GETS, req.childId, req.state, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
            latency += _mc->_ext_dram->access(ext_req);
            _num_miss_per_step++;

            // Then allocate and modify
            uint32_t victim_way = selectVictim(index);
            if (_cache[index].ways[victim_way].valid && _cache[index].ways[victim_way].dirty) {
                // Handle eviction using victim buffer
                if (_victim_buffer->reserveSlot()) {
                    Address victim_addr = _cache[index].ways[victim_way].tag * (_granularity * _num_sets) + index * _granularity;
                    _victim_buffer->addEntry(victim_addr, index, victim_way);
                    _victim_buffer_entries++;
                } else {
                    // Victim buffer full, writeback directly
                    Address victim_addr = _cache[index].ways[victim_way].tag * (_granularity * _num_sets) + index * _granularity;
                    MemReq wb_req = {victim_addr, PUTX, req.childId, nullptr, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
                    latency += _mc->_ext_dram->access(wb_req);
                }
            }
            _cache[index].ways[victim_way].tag = tag;
            _cache[index].ways[victim_way].valid = true;
            _cache[index].ways[victim_way].dirty = true;
        }
    }

    // Process any pending victim buffer entries if we have bandwidth
    if (_victim_buffer_entries > 0) {
        Address vb_addr;
        uint32_t vb_set, vb_way;
        if (_victim_buffer->getEntry(vb_addr, vb_set, vb_way)) {
            MemReq vb_req = {vb_addr, PUTX, req.childId, nullptr, req.cycle, req.childLock, req.initialState, req.srcId, req.flags};
            _mc->_ext_dram->access(vb_req);
            _victim_buffer_entries--;
        }
    }

    return req.cycle + latency;
}

void NDCScheme::period(MemReq& req) {
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

void NDCScheme::initStats(AggregateStat* parentStat) {
    AggregateStat* stats = new AggregateStat();
    stats->init("ndc", "NDC cache stats");

    // Add your NDC-specific stats here
    // Example:
    // _rowHits.init("rowHits", "Row buffer hits");
    // stats->append(&_rowHits);

    parentStat->append(stats);
}

// VictimBuffer implementation
VictimBuffer::VictimBuffer(uint32_t size) {
    _size = size;
    _numEntries = 0;
    _head = 0;
    _tail = 0;
    _reservedSlots = 0;
    _entries = new VictimBufferEntry[size];

    for (uint32_t i = 0; i < size; i++) {
        _entries[i].valid = false;
    }
}

VictimBuffer::~VictimBuffer() {
    delete[] _entries;
}

bool VictimBuffer::reserveSlot() {
    if (_numEntries + _reservedSlots >= _size) {
        return false;
    }

    _reservedSlots++;
    return true;
}

void VictimBuffer::releaseSlot() {
    if (_reservedSlots > 0) {
        _reservedSlots--;
    }
}

bool VictimBuffer::addEntry(Address address, uint32_t set, uint32_t way) {
    if (_numEntries >= _size) {
        return false;
    }

    _entries[_tail].valid = true;
    _entries[_tail].address = address;
    _entries[_tail].set = set;
    _entries[_tail].way = way;

    _tail = (_tail + 1) % _size;
    _numEntries++;

    if (_reservedSlots > 0) {
        _reservedSlots--;
    }

    return true;
}

bool VictimBuffer::getEntry(Address& address, uint32_t& set, uint32_t& way) {
    if (_numEntries == 0) {
        return false;
    }

    address = _entries[_head].address;
    set = _entries[_head].set;
    way = _entries[_head].way;

    _entries[_head].valid = false;
    _head = (_head + 1) % _size;
    _numEntries--;

    return true;
}
