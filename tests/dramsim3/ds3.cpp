#include "memory_hierarchy.h"
#include "dramsim3.h"

class DRAMSim3Memory : public MemObject {
private:
    dramsim3::MemorySystem* dramCore; // DRAMSim3 instance
    uint64_t curCycle;                // Track current cycle

public:
    DRAMSim3Memory(const std::string& configFile, const std::string& outputDir) {
        // Initialize DRAMSim3 with config file and callbacks
        dramCore = new dramsim3::MemorySystem(configFile, outputDir,
            [this](uint64_t addr) { this->readComplete(addr); },  // Read callback
            [this](uint64_t addr) { this->writeComplete(addr); }); // Write callback
        curCycle = 0;
    }

    // Handle memory requests from ZSim
    uint64_t access(MemReq& req) override {
        uint64_t respCycle = req.cycle + 1; // Minimum latency
        Address addr = req.lineAddr << 6;   // Assuming 64-byte lines
        bool isWrite = (req.type == PUTX);
        dramCore->AddTransaction(addr, isWrite); // Send to DRAMSim3
        return respCycle;
    }

    // Synchronize with DRAMSim3 clock
    uint32_t tick(uint64_t cycle) {
        dramCore->ClockTick();
        curCycle++;
        return 1;
    }

    void readComplete(uint64_t addr) {
        // Notify ZSim of read completion
    }

    void writeComplete(uint64_t addr) {
        // Notify ZSim of write completion
    }
};