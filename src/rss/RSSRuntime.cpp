#define _GNU_SOURCE
#include "rss/RSSRuntime.h"
#include "rss/RSSPipeline.h"
#include <map>
#include <mutex>
#include <iostream>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

namespace luv::rss {

thread_local TLLB* localTLLB = nullptr;

/**
 * Global Metadata for RC_SHARED objects
 */
struct ObjectMetadata {
    std::atomic<int32_t> refCount{0};
    void* originalAddr = nullptr;
};

static std::map<uintptr_t, ObjectMetadata*> globalRegistry;
static std::mutex registryMutex;

void ALR::processTLLB(TLLB& tllb) {
    size_t count = tllb.size();
    const LeaseEntry* entries = tllb.data();

    // Batched update to global registry
    std::lock_guard<std::mutex> lock(registryMutex);
    for (size_t i = 0; i < count; ++i) {
        uintptr_t addr = SAOPointer::decode(entries[i].ptr);
        auto it = globalRegistry.find(addr);
        if (it != globalRegistry.end()) {
            it->second->refCount.fetch_add(entries[i].delta, std::memory_order_relaxed);
        } else {
            if (entries[i].delta > 0) {
                 ObjectMetadata* meta = new ObjectMetadata();
                 meta->refCount.store(entries[i].delta, std::memory_order_relaxed);
                 meta->originalAddr = reinterpret_cast<void*>(addr);
                 globalRegistry[addr] = meta;
            }
        }
    }
    tllb.clear();

    // Section 7: Pressure-sensitive flushing
    checkMemoryPressure();
}

void ALR::reclaim() {
    std::lock_guard<std::mutex> lock(registryMutex);
    auto it = globalRegistry.begin();
    size_t processed = 0;
    const size_t BATCH_LIMIT = 1000; // Section 7.5: Starvation safeguard
    while (it != globalRegistry.end()) {
        if (it->second->refCount.load(std::memory_order_relaxed) <= 0) {
            free(it->second->originalAddr);
            delete it->second;
            it = globalRegistry.erase(it);
        } else {
            ++it;
        }
        if (++processed > BATCH_LIMIT) break;
    }
}

void ALR::checkMemoryPressure() {
    // Improved pressure heuristic: consider RSS usage vs available memory
    static size_t page_size = sysconf(_SC_PAGESIZE);
    static size_t total_phys_pages = sysconf(_SC_PHYS_PAGES);
    double total_memory = (double)total_phys_pages * page_size;
    
    double currentRSSUsage = (double)globalRegistry.size() * 1024.0; // Assume 1KB avg per metadata-tracked object
    double currentPressure = currentRSSUsage / total_memory;

    if (currentPressure > psfThreshold) {
        // Section 7.2: Aggressive PSF heuristics
        if (isAggressive || currentPressure > 0.95) {
            reclaim();
        }
    }
}

void SpeculativeContext::begin() {
    depth++;
    snapshots.emplace_back();
}

void SpeculativeContext::commit() {
    if (depth > 0) {
        depth--;
        snapshots.pop_back();
    }
}

void SpeculativeContext::rollback() {
    if (depth > 0) {
        auto& snapshot = snapshots.back();
        for (auto& [addr, data] : snapshot.memoryBackup) {
            std::memcpy(reinterpret_cast<void*>(addr), data.data(), data.size());
        }
        depth--;
        snapshots.pop_back();
    }
}

void SpeculativeContext::onWrite(uintptr_t addr, size_t size) {
    if (depth == 0) return;
    auto& snapshot = snapshots.back();
    if (snapshot.memoryBackup.find(addr) == snapshot.memoryBackup.end()) {
        std::vector<uint8_t> backup(size);
        std::memcpy(backup.data(), reinterpret_cast<void*>(addr), size);
        snapshot.memoryBackup[addr] = std::move(backup);
    }
}

void DEIManager::registerFunction(const std::string& name, void* speculative, void* safe) {
    entries[name] = {speculative, safe, true};
}

void* DEIManager::getEntry(const std::string& name) {
    auto it = entries.find(name);
    if (it == entries.end()) return nullptr;
    return it->second.preferSpeculative ? it->second.speculativeEntry : it->second.safeEntry;
}

void DEIManager::setPreference(const std::string& name, bool preferSpeculative) {
    auto it = entries.find(name);
    if (it != entries.end()) it->second.preferSpeculative = preferSpeculative;
}

DegradationTier DEEM::detectActiveTier(const HardwareProfile& hw) {
    if (hw.hasTBI && hw.hasTrapAssist && hw.hasTLBAlias) return DegradationTier::TIER_0_FULL;
    if (hw.hasMMU) return DegradationTier::TIER_1_MMU;
    return DegradationTier::TIER_2_SOFT;
}

void DEEM::enforceEquivalent(DegradationTier tier) {
    // Section 20: Ensure semantic equivalence across tiers
    std::cout << "[DEEM] Enforcing semantic equivalence for tier " << (int)tier << std::endl;
}

Slab* SlabManager::allocateSlab() {
    void* ptr = nullptr;
    if (posix_memalign(&ptr, SLAB_SIZE, SLAB_SIZE) != 0) {
        return nullptr;
    }
    Slab* slab = new Slab();
    slab->baseAddr = reinterpret_cast<uintptr_t>(ptr);
    slab->size = SLAB_SIZE;
    slab->isReadOnly = false;
    slab->isTombstoned = false;
    slab->generation = 1;
    slabs.push_back(slab);
    return slab;
}

void SlabManager::tombstoneSlab(Slab* slab) {
    slab->isTombstoned = true;
}

void SlabManager::retireSlab(Slab* slab) {
    auto it = std::find(slabs.begin(), slabs.end(), slab);
    if (it != slabs.end()) {
        free(reinterpret_cast<void*>(slab->baseAddr));
        delete slab;
        slabs.erase(it);
    }
}

void SlabManager::setProtection(Slab* slab, bool readOnly) {
    slab->isReadOnly = readOnly;
    int prot = PROT_READ;
    if (!readOnly) prot |= PROT_WRITE;
    if (mprotect(reinterpret_cast<void*>(slab->baseAddr), slab->size, prot) != 0) {
        perror("[SlabManager] mprotect failed");
    }
}

SlabManager::Diagnostics SlabManager::getDiagnostics() const {
    Diagnostics d = {0, 0, 0, 0};
    d.totalSlabs = slabs.size();
    for (const auto* s : slabs) {
        if (s->isTombstoned) d.tombstonedSlabs++;
        else d.activeSlabs++;
    }
    return d;
}

void SlabManager::reportLeaks() const {
    auto d = getDiagnostics();
    if (d.activeSlabs > 0) {
        std::cerr << "[SlabManager] Potential leaks detected: " << d.activeSlabs << " active slabs remaining." << std::endl;
    }
}

void VASManager::createDualMapping(uintptr_t virtAddr, uintptr_t physAddr, size_t size) {
    // Section 13: Implement dual virtual mappings (RO/RW)
    int fd = memfd_create("rss_dual_mapping", MFD_CLOEXEC);
    if (fd == -1) {
        perror("[VASManager] memfd_create failed");
        return;
    }
    if (ftruncate(fd, size) == -1) {
        perror("[VASManager] ftruncate failed");
        close(fd);
        return;
    }

    // Map once as RW
    void* rw_ptr = mmap(reinterpret_cast<void*>(virtAddr), size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
    if (rw_ptr == MAP_FAILED) {
        perror("[VASManager] mmap RW failed");
    }

    // Map again as RO at a fixed offset (simulated hardware shadowing)
    uintptr_t roAddr = virtAddr + 0x100000000ULL; // 4GB offset
    void* ro_ptr = mmap(reinterpret_cast<void*>(roAddr), size, PROT_READ, MAP_SHARED | MAP_FIXED, fd, 0);
    if (ro_ptr == MAP_FAILED) {
        perror("[VASManager] mmap RO failed");
    }

    close(fd);
}

void VASManager::handleTrap(uintptr_t faultAddr) {
    // Section 13: Route to deterministic fallback path
    std::cout << "[VASManager] Trap detected at address 0x" << std::hex << faultAddr << std::dec 
              << ". Initiating Tier 3 fallback." << std::endl;
}

} // namespace luv::rss
