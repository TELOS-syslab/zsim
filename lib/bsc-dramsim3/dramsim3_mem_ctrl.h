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

#ifndef DRAMSIM3_MEM_CTRL_H_
#define DRAMSIM3_MEM_CTRL_H_

#include <functional>
#include <map>
#include <string>
#include "g_std/g_string.h"
#include "g_std/g_unordered_map.h"
#include "memory_hierarchy.h"
#include "pad.h"
#include "stats.h"
#include "config.h"

namespace dramsim3
{
class MemorySystem;
struct Address;
}; // namespace dramsim3

class DRAMSim3AccEvent;

/*struct DS3Request
{
    DS3Request(uint64_t addr, uint64_t cycle) : 
        addr(addr), channel(0), rank(0), bank(0), row(0),
        added_cycle(cycle) {}
    uint64_t addr;
    uint64_t channel;
    uint64_t rank;
    uint64_t bank;
    uint64_t row;
    uint64_t added_cycle;
};*/

class DRAMSim3Memory : public MemObject
{ //one DRAMSim3 controller
  private:
    g_string name;
    uint32_t minLatency;
    uint32_t domain;

    dramsim3::MemorySystem *dramCore;

    std::multimap<uint64_t, DRAMSim3AccEvent *> inflightRequests;

    uint64_t curCycle; //processor cycle, used in callbacks
    uint64_t dramCycle;

    // R/W stats
    PAD();
    Counter profReads;
    Counter profWrites;
    Counter profTotalRdLat;
    Counter profTotalWrLat;
    PAD();

  public:
    DRAMSim3Memory(std::string &ConfigName, std::string &OutputDir,
                   int cpuFreqMHz, uint32_t _domain, const g_string &_name);

    const char *getName() { return name.c_str(); }

    void initStats(AggregateStat *parentStat);

    // Record accesses
    uint64_t access(MemReq& req, int type, uint32_t data_size = 4);
    uint64_t access(MemReq &req);
    void setDRAMsimConfiguration(uint32_t delayQueue); // Enable sim config in DRAMSim3 side.
    // Event-driven simulation (phase 2)
    uint32_t tick(uint64_t cycle);
    void enqueue(DRAMSim3AccEvent *ev, uint64_t cycle);

  private:
    // if you want to use any data structure at all you in access()
    // it has to be on the zsim side
   // using BankQueue = g_vector<DS3Request>;
   // using RankQueue = g_unordered_map<uint64_t, BankQueue>;
   // using ChannelQueue = g_unordered_map<uint64_t, RankQueue>;
   // g_unordered_map<uint64_t, ChannelQueue> requestQueues;

    uint64_t channelMask, rankMask, bankMask, rowMask;
    void DRAM_read_return_cb(uint64_t addr);
    void DRAM_write_return_cb(uint64_t addr);
    std::function<void(uint64_t)> callBackFn;
    uint64_t dramPsPerClk, cpuPsPerClk;
    uint64_t dramPs, cpuPs;
    int tCL;
};

class SplitAddrMemory : public MemObject {

  private:
      const g_vector<MemObject*> mems;
      const g_string name;
      uint32_t _mapping_granu;

  public:
    SplitAddrMemory(const g_vector<MemObject*>& _mems, const char* _name, Config& config) 
			: mems(_mems), name(_name) 
		{
      // 64 cachelines = 4096 bytes (page granularity mapping)
      _mapping_granu = config.get<uint32_t>("sys.mem.mapGranu", 64); 
		}
    // set the delay for no man's land delay buffer (Rommel Sanchez et al)
    void setDRAMsimConfiguration(uint32_t delayQueue)
    {
        // printf("size of mems available at ZSim side is: %d \n", mems.size()); // size is two
        // mems[0]->setDRAMsimConfiguration(delayQueue);
        for (auto mem : mems) mem->setDRAMsimConfiguration(delayQueue);
    }
    uint64_t access(MemReq& req) {
      Address addr = req.lineAddr;
			uint32_t mem = (addr / _mapping_granu) % mems.size();
			Address sel1 = addr / _mapping_granu / mems.size();
			Address sel2 = addr % _mapping_granu;
			req.lineAddr = sel1 * _mapping_granu + sel2;  
      //uint32_t mem = addr % mems.size();
      //Address ctrlAddr = addr/mems.size();
      //req.lineAddr = ctrlAddr;
      uint64_t respCycle = mems[mem]->access(req);
      req.lineAddr = addr;
      return respCycle;
    }

    const char* getName() {
        return name.c_str();
    }

    void initStats(AggregateStat* parentStat) {
        for (auto mem : mems) mem->initStats(parentStat);
    }
};

#endif // DRAMSIM3_MEM_CTRL_H_
