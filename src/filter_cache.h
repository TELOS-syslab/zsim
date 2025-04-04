/** $lic$
 * Copyright (C) 2012-2015 by Massachusetts Institute of Technology
 * Copyright (C) 2010-2013 by The Board of Trustees of Stanford University
 *
 * This file is part of zsim.
 *
 * zsim is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, version 2.
 *
 * If you use this software in your research, we request that you reference
 * the zsim paper ("ZSim: Fast and Accurate Microarchitectural Simulation of
 * Thousand-Core Systems", Sanchez and Kozyrakis, ISCA-40, June 2013) as the
 * source of the simulator in any publications that use this software, and that
 * you send us a citation of your work.
 *
 * zsim is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef FILTER_CACHE_H_
#define FILTER_CACHE_H_

#include "bithacks.h"
#include "cache.h"
#include "assert.h"
#include "galloc.h"
#include "zsim.h"
#include "g_std/g_unordered_map.h"
#include "g_std/g_unordered_set.h"
#include "config.h"
/* Extends Cache with an L0 direct-mapped cache, optimized to hell for hits
 *
 * L1 lookups are dominated by several kinds of overhead (grab the cache locks,
 * several virtual functions for the replacement policy, etc.). This
 * specialization of Cache solves these issues by having a filter array that
 * holds the most recently used line in each set. Accesses check the filter array,
 * and then go through the normal access path. Because there is one line per set,
 * it is fine to do this without grabbing a lock.
 */

class FilterCache : public Cache {
    private:
        struct FilterEntry {
            volatile Address rdAddr;
            volatile Address wrAddr;
            volatile uint64_t availCycle;

            void clear() {wrAddr = 0; rdAddr = 0; availCycle = 0;}
        };

        //Replicates the most accessed line of each set in the cache
        FilterEntry* filterArray;
        Address setMask;
        uint32_t numSets;
        uint32_t srcId; //should match the core
        uint32_t reqFlags;
        g_vector<MemObject*> ancestors; // Bypass cache system with a pointer to send informaiton to DRAMSim3
        lock_t filterLock;
        uint64_t fGETSHit, fGETXHit;
		// this is not an accurate tlb. It just randomize the page nums   
		bool _enable_tlb;
        bool _enable_johnny;
        uint64_t _johnny_ptr;
        uint64_t _mem_size;
		drand48_data _buffer;
		g_unordered_map <Address, Address> _tlb;
		g_unordered_set <Address> _exist_pgnum; 
    public:
        FilterCache(uint32_t _numSets, uint32_t _numLines, CC* _cc, CacheArray* _array,
                ReplPolicy* _rp, uint32_t _accLat, uint32_t _invLat, g_string& _name, Config &config)
            : Cache(_numLines, _cc, _array, _rp, _accLat, _invLat, _name)
        {
            numSets = _numSets;
            setMask = numSets - 1;
            filterArray = gm_memalign<FilterEntry>(CACHE_LINE_BYTES, numSets);
            for (uint32_t i = 0; i < numSets; i++) filterArray[i].clear();
            futex_init(&filterLock);
            fGETSHit = fGETXHit = 0;
            srcId = -1;
            reqFlags = 0;
			_enable_tlb = config.get<bool>("sim.enableTLB", false);
            _enable_johnny = config.get<bool>("sim.enableJohnny", false);
            _johnny_ptr = 0;
            _mem_size = (uint64_t)config.get<uint32_t>("sim.memSize", 0) << 20;
            if (_mem_size == 0) {
                _mem_size = 0x0000ffffffffffff; // 48bit address space, 281474976710656(256TB)
            }
            info("FilterCache: tlb enabled = %d, johnny enabled = %d, memSize = %ld Bytes", _enable_tlb, _enable_johnny, _mem_size);
			srand48_r((uint64_t)this, &_buffer);
        }

        // Configure No man's land delay (Rommel Sanchez et al)
        void setAncestors(const g_vector<MemObject*>& _parents, uint32_t delayQueue){
            ancestors.resize(_parents.size());
            for (uint32_t p = 0; p < ancestors.size(); p++) {
                ancestors[p] = _parents[p];
                ancestors[p]->setDRAMsimConfiguration(delayQueue);
            }
        }

        void setSourceId(uint32_t id) {
            srcId = id;
        }

        void setFlags(uint32_t flags) {
            reqFlags = flags;
        }

        void initStats(AggregateStat* parentStat) {
            AggregateStat* cacheStat = new AggregateStat();
            cacheStat->init(name.c_str(), "Filter cache stats");

            ProxyStat* fgetsStat = new ProxyStat();
            fgetsStat->init("fhGETS", "Filtered GETS hits", &fGETSHit);
            ProxyStat* fgetxStat = new ProxyStat();
            fgetxStat->init("fhGETX", "Filtered GETX hits", &fGETXHit);
            cacheStat->append(fgetsStat);
            cacheStat->append(fgetxStat);

            initCacheStats(cacheStat);
            parentStat->append(cacheStat);
        }

        inline uint64_t load(Address vAddr, uint64_t curCycle) {
            Address vLineAddr = vAddr >> lineBits;
            // info("load: vLineAddr = %lx", vLineAddr);
            uint32_t idx = vLineAddr & setMask;
            uint64_t availCycle = filterArray[idx].availCycle; //read before, careful with ordering to avoid timing races
            if (vLineAddr == filterArray[idx].rdAddr) {
                fGETSHit++;
                return MAX(curCycle, availCycle);
            } else {
                return replace(vLineAddr, idx, true, curCycle);
            }
        }

        inline uint64_t store(Address vAddr, uint64_t curCycle) {
            Address vLineAddr = vAddr >> lineBits;
            // info("store: vLineAddr = %lx", vLineAddr);
            uint32_t idx = vLineAddr & setMask;
            uint64_t availCycle = filterArray[idx].availCycle; //read before, careful with ordering to avoid timing races
            if (vLineAddr == filterArray[idx].wrAddr) {
                fGETXHit++;
                //NOTE: Stores don't modify availCycle; we'll catch matches in the core
                //filterArray[idx].availCycle = curCycle; //do optimistic store-load forwarding
                return MAX(curCycle, availCycle);
            } else {
                return replace(vLineAddr, idx, false, curCycle);
            }
        }

        uint64_t replace(Address vLineAddr, uint32_t idx, bool isLoad, uint64_t curCycle) {
			Address pLineAddr;
			// page num = vLineAddr shifted by 6 bits. So it is shifted by 12 bits in total (4KB page size)
			if (_enable_tlb) {
				Address vpgnum = vLineAddr >> 6; // Virtual page number
				uint64_t pgnum;
				futex_lock(&filterLock);
				if (_tlb.find(vpgnum) == _tlb.end()) {
					if (_enable_johnny) {
						pgnum = _johnny_ptr;
						_johnny_ptr++;
						if (_johnny_ptr >= _mem_size >> 6) {
							_johnny_ptr = 0;
							info("FilterCache: johnny_ptr reached max memory size, reset to 0");
						}
                        assert(_exist_pgnum.find(pgnum) == _exist_pgnum.end());
					} else {
						do {
							int64_t rand;
							lrand48_r(&_buffer, &rand);
							pgnum = rand & 0x000fffffffffffff;
						} while (_exist_pgnum.find(pgnum) != _exist_pgnum.end());
					}
					_tlb[vpgnum] = pgnum;
					_exist_pgnum.insert(pgnum);
				} else {
					pgnum = _tlb[vpgnum];
				}

				pLineAddr = procMask | (pgnum << 6) | (vLineAddr & 0x3f);
			} else {
                pLineAddr = procMask | vLineAddr;
                futex_lock(&filterLock);
            }
            MESIState dummyState = MESIState::I;
            MemReq req = {pLineAddr, isLoad? GETS : GETX, 0, &dummyState, curCycle, &filterLock, dummyState, srcId, reqFlags};
            uint64_t respCycle  = access(req);

            //Due to the way we do the locking, at this point the old address might be invalidated, but we have the new address guaranteed until we release the lock

            //Careful with this order
            Address oldAddr = filterArray[idx].rdAddr;
            filterArray[idx].wrAddr = isLoad? -1L : vLineAddr;
            filterArray[idx].rdAddr = vLineAddr;

            //For LSU simulation purposes, loads bypass stores even to the same line if there is no conflict,
            //(e.g., st to x, ld from x+8) and we implement store-load forwarding at the core.
            //So if this is a load, it always sets availCycle; if it is a store hit, it doesn't
            if (oldAddr != vLineAddr) filterArray[idx].availCycle = respCycle;

            futex_unlock(&filterLock);
            return respCycle;
        }

        uint64_t invalidate(const InvReq& req) {
            Cache::startInvalidate();  // grabs cache's downLock
            futex_lock(&filterLock);
            uint32_t idx = req.lineAddr & setMask; //works because of how virtual<->physical is done...
            if ((filterArray[idx].rdAddr | procMask) == req.lineAddr) { //FIXME: If another process calls invalidate(), procMask will not match even though we may be doing a capacity-induced invalidation!
                filterArray[idx].wrAddr = -1L;
                filterArray[idx].rdAddr = -1L;
            }
            uint64_t respCycle = Cache::finishInvalidate(req); // releases cache's downLock
            futex_unlock(&filterLock);
            return respCycle;
        }

        void contextSwitch() {
            futex_lock(&filterLock);
            for (uint32_t i = 0; i < numSets; i++) filterArray[i].clear();
            futex_unlock(&filterLock);
        }
};

#endif  // FILTER_CACHE_H_