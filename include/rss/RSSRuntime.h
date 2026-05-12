#pragma once

#include <cstdint>
#include <vector>
#include <atomic>
#include <map>
#include <string>

namespace luv::rss {

struct HardwareProfile;

/**
 * Segmented Alias Offsetting (SAO) Pointer Layout
 * 
 * High 8 bits (63-56) - TBI (Top Byte Ignore) region:
 * [63]    - NEN (Null) bit: 1 if null, 0 if valid
 * [62-60] - RSS State:
 *           000: UNIQUE
 *           001: LENT
 *           010: OWNED_COW
 *           011: RC_SHARED
 *           100: IMMUTABLE
 *           101: ESCAPED
 *           111: INVALID/FREED
 * [59-56] - Region ID / Generation Tag
 */

enum class SAOState : uint8_t {
    UNIQUE    = 0,
    LENT      = 1,
    OWNED_COW = 2,
    RC_SHARED = 3,
    IMMUTABLE = 4,
    ESCAPED   = 5,
    INVALID   = 7
};

class SAOPointer {
public:
    static constexpr uint64_t NEN_BIT = 1ULL << 63;
    static constexpr uint64_t STATE_MASK = 0x7ULL << 60;
    static constexpr uint64_t TAG_MASK = 0xFULL << 56;
    static constexpr uint64_t ADDR_MASK = 0x00FFFFFFFFFFFFFFULL;
    static constexpr uint64_t CFI_MASK = 0xFFULL << 48; // Section 25: CFI bits

    static uint64_t encode(uintptr_t addr, SAOState state, bool isNull = false) {
        uint64_t ptr = static_cast<uint64_t>(addr) & ADDR_MASK;
        if (isNull) ptr |= NEN_BIT;
        ptr |= (static_cast<uint64_t>(state) << 60) & STATE_MASK;
        // Section 25: Simple CFI tag (hash of address and state)
        uint64_t cfi = ((ptr ^ (ptr >> 8)) & 0xFFULL);
        ptr |= (cfi << 48);
        return ptr;
    }

    static bool validateCFI(uint64_t ptr) {
        uint64_t cfi = (ptr & CFI_MASK) >> 48;
        uint64_t expected = ((ptr & ~CFI_MASK) ^ ((ptr & ~CFI_MASK) >> 8)) & 0xFFULL;
        return cfi == expected;
    }

    static SAOState getState(uint64_t ptr) {
        return static_cast<SAOState>((ptr & STATE_MASK) >> 60);
    }

    static bool isNull(uint64_t ptr) {
        return (ptr & NEN_BIT) != 0;
    }

    static uintptr_t decode(uint64_t ptr) {
        return static_cast<uintptr_t>(ptr & ADDR_MASK);
    }

    static uint64_t transition(uint64_t ptr, SAOState newState) {
        return (ptr & ~STATE_MASK) | ((static_cast<uint64_t>(newState) << 60) & STATE_MASK);
    }

    static uint64_t markInvalid(uint64_t ptr) {
        return transition(ptr, SAOState::INVALID);
    }

    static uint64_t setNull(uint64_t ptr, bool isNull) {
        if (isNull) return ptr | NEN_BIT;
        else return ptr & ~NEN_BIT;
    }
};

/**
 * Section 19: Speculative Concurrency
 */
struct Snapshot {
    std::map<uintptr_t, std::vector<uint8_t>> memoryBackup;
    std::map<uintptr_t, uint64_t> pointerBackup;
};

class SpeculativeContext {
public:
    void begin();
    void commit();
    void rollback();
    void onWrite(uintptr_t addr, size_t size);
    
    bool isSpeculative() const { return depth > 0; }
    
private:
    int depth = 0;
    std::vector<Snapshot> snapshots;
};

/**
 * Section 18: DEI (Dual-Entry Inlining)
 */
struct DEIEntry {
    void* speculativeEntry;
    void* safeEntry;
    bool preferSpeculative;
};

class DEIManager {
public:
    void registerFunction(const std::string& name, void* speculative, void* safe);
    void* getEntry(const std::string& name);
    void setPreference(const std::string& name, bool preferSpeculative);

private:
    std::map<std::string, DEIEntry> entries;
};

/**
 * Section 20: DEEM + HIL (Degradation tiers)
 */
enum class DegradationTier {
    TIER_0_FULL,    // Hardware-accelerated fast path
    TIER_1_MMU,     // MMU-only shadowing
    TIER_2_SOFT,    // Software-only fallback
    TIER_3_SAFE     // Ultra-safe skeleton fallback
};

class DEEM {
public:
    static DegradationTier detectActiveTier(const HardwareProfile& hw);
    static void enforceEquivalent(DegradationTier tier);
};

/**
 * Thread-Local Lease Buffer (TLLB)
 * 
 * High-performance buffer for recording reference count changes or 
 * memory releases that need asynchronous reclamation.
 */
struct LeaseEntry {
    uint64_t ptr;
    uint32_t delta; // +1 for Retain, -1 for Release
    uint32_t padding;
};

class TLLB {
public:
    static constexpr size_t DEFAULT_CAPACITY = 1024;

    TLLB(size_t capacity = DEFAULT_CAPACITY) 
        : buffer(capacity), head(0), capacity(capacity) {}

    bool push(const LeaseEntry& entry) {
        size_t h = head.load(std::memory_order_relaxed);
        if (h >= capacity) return false;
        buffer[h] = entry;
        head.store(h + 1, std::memory_order_release);
        return true;
    }

    void clear() {
        head.store(0, std::memory_order_relaxed);
    }

    size_t size() const {
        return head.load(std::memory_order_acquire);
    }

    const LeaseEntry* data() const {
        return buffer.data();
    }

private:
    std::vector<LeaseEntry> buffer;
    std::atomic<size_t> head;
    size_t capacity;
};

// Thread-local TLLB for NUMA-awareness
extern thread_local TLLB* localTLLB;

/**
 * Slab-Mapped Shadowing (SMS)
 * 
 * Manages 64MB slabs with RO/RW dual mappings for COW and 
 * transactional safety.
 */
struct Slab {
    uintptr_t baseAddr;
    size_t size;
    bool isReadOnly;
    bool isTombstoned; // Deferred reclamation for speculative safety
    uint32_t generation;
};

class SlabManager {
public:
    static constexpr size_t SLAB_SIZE = 64 * 1024 * 1024; // 64MB

    Slab* allocateSlab();
    void tombstoneSlab(Slab* slab);
    void retireSlab(Slab* slab);
    void setProtection(Slab* slab, bool readOnly);

    // Section 11: Slab Diagnostics
    struct Diagnostics {
        size_t totalSlabs;
        size_t activeSlabs;
        size_t tombstonedSlabs;
        size_t fragmentationBytes;
    };
    Diagnostics getDiagnostics() const;
    void reportLeaks() const;

private:
    std::vector<Slab*> slabs;
};

/**
 * Virtual Alias Shadowing (VAS)
 * 
 * Handles dual virtual mappings (RO/RW) and trap-to-fallback routing.
 */
class VASManager {
public:
    void createDualMapping(uintptr_t virtAddr, uintptr_t physAddr, size_t size);
    void handleTrap(uintptr_t faultAddr);
};

/**
 * Trap / Signal Bridge
 */
class TrapHandler {
public:
    static void install() {
        // Install SIGSEGV/SIGBUS handlers for VAS/SAO traps
    }

    static void onTrap(int sig, void* info, void* ctx) {
        // Deterministic recovery and fallback skeleton entry
    }
};

/**
 * Asynchronous Lease Reclaimer (ALR)
 * 
 * Global worker that processes TLLBs and performs the actual 
 * reclamation/freeing of RC_SHARED memory.
 */
class ALR {
public:
    ALR() : psfThreshold(0.8), isAggressive(false) {}

    void processTLLB(TLLB& tllb);
    void reclaim();

    // Section 7: PSF (Pressure-Sensitive Flushing)
    void setPSFThreshold(double threshold) { psfThreshold = threshold; }
    void setAggressive(bool aggressive) { isAggressive = aggressive; }
    
    void checkMemoryPressure();

private:
    double psfThreshold;
    bool isAggressive;
};

} // namespace luv::rss
