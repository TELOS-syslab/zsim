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

#include "dramsim3_mem_ctrl.h"
#include <map>
#include <string>
#include "event_recorder.h"
#include "tick_event.h"
#include "timing_event.h"
#include "zsim.h"

// add for trace 
#include <fstream>
#include <iostream>

#ifdef _WITH_DRAMSIM3_ //was compiled with dramsim3
//#include "memory_system.h"
#include "dramsim3.h"

using namespace std;

class DRAMSim3AccEvent : public TimingEvent
{
  private:
    DRAMSim3Memory *dram;
    bool write;
    Address addr;

  public:
    uint64_t sCycle;

    DRAMSim3AccEvent(DRAMSim3Memory *_dram, bool _write, Address _addr, int32_t domain) : TimingEvent(0, 0, domain), dram(_dram), write(_write), addr(_addr) {}

    bool isWrite() const
    {
        return write;
    }

    Address getAddr() const
    {
        return addr;
    }

    void simulate(uint64_t startCycle)
    {
        sCycle = startCycle;
        dram->enqueue(this, startCycle);
    }
};

DRAMSim3Memory::DRAMSim3Memory(std::string& ConfigName, std::string& OutputDir,
                               int cpuFreqMHz, uint32_t _domain, const g_string &_name)
{
    curCycle = 0;
    dramCycle = 0;
    dramPs = 0;
    cpuPs = 0;
    minLatency = 1;
    // NOTE: this will alloc DRAM on the heap and not the glob_heap, make sure only one process ever handles this
    callBackFn = std::bind(&DRAMSim3Memory::DRAM_read_return_cb, this, std::placeholders::_1);

    // For some reason you cannot "new" here because zsim seems to override this "new"
    // so we have to use the helper function to init the pointer
    dramCore = dramsim3::GetMemorySystem(ConfigName, OutputDir, callBackFn, callBackFn);
    double tCK = dramCore->GetTCK();
    channelMask = dramCore->GetChannelMask();
    rankMask = dramCore->GetRankMask();
    bankMask = dramCore->GetBankMask();
    rowMask = dramCore->GetRowMask();
    info("DRAMSim3Memory: tCK=%f, channelMask=%d, rankMask=%d, bankMask=%d, rowMask=%d", tCK, channelMask, rankMask, bankMask, rowMask);

    dramPsPerClk = static_cast<uint64_t>(tCK*1000);
    cpuPsPerClk = static_cast<uint64_t>(1000000. / cpuFreqMHz);
    assert(cpuPsPerClk < dramPsPerClk);
    domain = _domain;
    TickEvent<DRAMSim3Memory> *tickEv = new TickEvent<DRAMSim3Memory>(this, domain);
    tickEv->queue(0); // start the sim at time 0

    name = _name;
}

void DRAMSim3Memory::initStats(AggregateStat *parentStat)
{
    AggregateStat* memStats = new AggregateStat();
    memStats->init(name.c_str(), "Memory controller stats");
    profReads.init("rd", "Read requests"); memStats->append(&profReads);
    profWrites.init("wr", "Write requests"); memStats->append(&profWrites);
    profTotalRdLat.init("rdlat", "Total latency experienced by read requests"); memStats->append(&profTotalRdLat);
    profTotalWrLat.init("wrlat", "Total latency experienced by write requests"); memStats->append(&profTotalWrLat);
    parentStat->append(memStats);
}

// Enable sim config in DRAMSim3 side.
void DRAMSim3Memory::setDRAMsimConfiguration(uint32_t delayQueue)
{
    dramCore->setDelayQueue(delayQueue);
} 

uint64_t DRAMSim3Memory::access(MemReq& req) {
	return access(req, 0, 1);
}

uint64_t DRAMSim3Memory::access(MemReq& req, int type, uint32_t data_size) {
    if (!dramCore) {
        panic("DRAMSim3: Trying to access uninitialized memory system");
    }

    switch (req.type) {
        case PUTS:
        case PUTX:
            *req.state = I;
            break;
        case GETS:
            *req.state = req.is(MemReq::NOEXCL)? S : E;
            break;
        case GETX:
            *req.state = M;
            break;

        default: panic("!?");
    }

    //uint64_t respCycle = req.cycle + minLatency;
    uint64_t respCycle = req.cycle + minLatency + data_size;
    assert(respCycle > req.cycle);

    if ((req.type != PUTS /*discard clean writebacks*/) && zinfo->eventRecorders[req.srcId]) {
        Address addr = req.lineAddr << lineBits;
        uint64_t hexAddr = (uint64_t)addr;
        if (!addr) {
            warn("DRAMSim3: Received access to address 0");
        }
        
        bool isWrite = (req.type == PUTX);
        DRAMSim3AccEvent* memEv = new (zinfo->eventRecorders[req.srcId]) DRAMSim3AccEvent(this, isWrite, addr, domain);
        if (!memEv) {
            panic("DRAMSim3: Failed to create access event");
        }
		if (type == 0) { // default. The only record. 
	        memEv->setMinStartCycle(req.cycle);
    	    TimingRecord tr = {addr, req.cycle, respCycle, req.type, memEv, memEv};
			for (uint32_t i = 1; data_size > i * 4; i++) {
        		DRAMSim3AccEvent* ev = new (zinfo->eventRecorders[req.srcId]) DRAMSim3AccEvent(this, isWrite, addr + 64 * i, domain);
    	    	tr.endEvent->addChild(ev, zinfo->eventRecorders[req.srcId]);
				tr.endEvent = ev;
			}
        	zinfo->eventRecorders[req.srcId]->pushRecord(tr);
		} else if (type == 1) { // append the current event to the end of the previous one
       	 	TimingRecord tr = zinfo->eventRecorders[req.srcId]->popRecord();
           	memEv->setMinStartCycle(tr.reqCycle);
			assert(tr.endEvent);
			tr.endEvent->addChild(memEv, zinfo->eventRecorders[req.srcId]);
			// XXX when to update respCycle 
			//tr.respCycle = respCycle;
			tr.type = req.type;
			tr.endEvent = memEv;
			for (uint32_t i = 1; data_size > i * 4; i++) {
        		DRAMSim3AccEvent* ev = new (zinfo->eventRecorders[req.srcId]) DRAMSim3AccEvent(this, isWrite, addr + 64 * i, domain);
    	    	tr.endEvent->addChild(ev, zinfo->eventRecorders[req.srcId]);
				tr.endEvent = ev;
			}	
       	 	zinfo->eventRecorders[req.srcId]->pushRecord(tr);
		} else if (type == 2) { 
			// append the current event to the end of the previous one
			// but the current event is not on the critical path
       	 	TimingRecord tr = zinfo->eventRecorders[req.srcId]->popRecord();
           	memEv->setMinStartCycle(tr.reqCycle);
			assert(tr.endEvent);
			tr.endEvent->addChild(memEv, zinfo->eventRecorders[req.srcId]);
			DRAMSim3AccEvent * last_ev = memEv;
			for (uint32_t i = 1; data_size > i * 4; i++) {
        		DRAMSim3AccEvent* ev = new (zinfo->eventRecorders[req.srcId]) DRAMSim3AccEvent(this, isWrite, addr + 64 * i, domain);
    	    	last_ev->addChild(ev, zinfo->eventRecorders[req.srcId]);
				last_ev = ev;
/*				static int k = 0;
				k ++;
				if (k % 10000 == 0)
					printf("k=%d, i=%d. data_size=%d\n", k++, i, data_size);
					*/
			}
			//tr.respCycle = respCycle;
			tr.type = req.type;
       	 	zinfo->eventRecorders[req.srcId]->pushRecord(tr);
		}

    }

    return respCycle;
}

uint32_t DRAMSim3Memory::tick(uint64_t cycle) {
    cpuPs += cpuPsPerClk;
    curCycle++;
    if (cpuPs > dramPs) {
        dramCore->ClockTick();
        dramPs += dramPsPerClk;
        dramCycle++;
    }
    if (cpuPs == dramPs) {  // reset to prevent overflow
        cpuPs = 0;
        dramPs = 0;
    }
    return 1;
}

void DRAMSim3Memory::enqueue(DRAMSim3AccEvent *ev, uint64_t cycle) {
    // info("[%s] %s access to %lx added at %ld, %ld inflight reqs", getName(), ev->isWrite()? "Write" : "Read", ev->getAddr(), cycle, inflightRequests.size());
    dramCore->AddTransaction(ev->getAddr(), ev->isWrite());
    inflightRequests.insert(std::pair<Address, DRAMSim3AccEvent *>(ev->getAddr(), ev));
    ev->hold();

}

void DRAMSim3Memory::DRAM_read_return_cb(uint64_t addr) {
    std::multimap<uint64_t, DRAMSim3AccEvent *>::iterator it = inflightRequests.find(addr);
    assert((it != inflightRequests.end()));
    DRAMSim3AccEvent *ev = it->second;

    uint32_t lat = curCycle + 1 - ev->sCycle;
    minLatency = lat;
    
    if (ev->isWrite()) {
        profWrites.inc();
        profTotalWrLat.inc(lat);
    } else {
        profReads.inc();
        profTotalRdLat.inc(lat);
    }

    ev->release();
    ev->done(curCycle + 1);
    inflightRequests.erase(it);
    // info("[%s] %s access to %lx DONE at %ld (%ld cycles), %ld inflight reqs", getName(), it->second->isWrite()? "Write" : "Read", it->second->getAddr(), curCycle, curCycle-it->second->sCycle, inflightRequests.size());
}

void DRAMSim3Memory::DRAM_write_return_cb(uint64_t addr)
{
    //Same as read for now
    DRAM_read_return_cb(addr);
}

#else //no dramsim3, have the class fail when constructed

using std::string;

DRAMSim3Memory::DRAMSim3Memory(std::string& ConfigName, std::string& OutputDir,
                               int cpuFreqMHz, uint32_t _domain, const g_string &_name)
{
    panic("Cannot use DRAMSim3Memory, zsim was not compiled with DRAMSim3");
}

void DRAMSim3Memory::initStats(AggregateStat *parentStat) { panic("???"); }
uint64_t DRAMSim3Memory::access(MemReq &req)
{
    panic("???");
    return 0;
}
uint64_t DRAMSim3Memory::access(MemReq& req, int type, uint32_t data_size) { panic("???"); return 0; }
uint32_t DRAMSim3Memory::tick(uint64_t cycle)
{
    panic("???");
    return 0;
}
void DRAMSim3Memory::enqueue(DRAMSim3AccEvent *ev, uint64_t cycle) { panic("???"); }
void DRAMSim3Memory::DRAM_read_return_cb(uint64_t addr) { panic("???"); }
void DRAMSim3Memory::DRAM_write_return_cb(uint64_t addr) { panic("???"); }

#endif
