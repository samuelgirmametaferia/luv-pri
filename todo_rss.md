Here is the updated **RSS v1.1 Implementation Todo (Unified Final)**, integrating the architectural feedback (Metaprogramming, Nen-presence, Effect Systems) and mitigating the identified risks (Lease Lag, TLB Shootdowns, Transactional `FREE`s, and Linearity). 

I have also added three of my own high-value architectural suggestions (marked with 🌟) regarding **NUMA-awareness**, **Debugger Tooling**, and **Control Flow Integrity (CFI)**.

---

# RSS v1.1 Implementation Todo (Unified Final)

This file tracks all work needed to deliver **RSS v1.1**, including core spec features, hardware accelerations, and additional engineering tasks required for a production-grade release.

## 0. Program Management / Scope Lock

- [x] Freeze RSS v1.1 terminology and acronyms (RSS, SAO, VAS, SMS, ALR, TLLB, PSF, DEI, DEEM, HIL).
- [x] Define explicit non-goals for v1.1 (what is deferred to v1.2+).
- [x] Create feature gates per subsystem (compile-time + runtime toggles).
- [x] Add architecture decision records (ADRs) for each major design choice.
- [x] Establish success metrics (correctness, throughput, latency, memory footprint, degradation behavior).

*(Sub-sections 0.1 to 0.5 remain locked and completed as per previous spec)*

## 1. Frontend + Analysis Pipeline (AST/CFG/MFG/RSS)

- [x] Add/confirm IR entry points: AST -> CFG -> MFG -> RSS analysis.
- [x] Implement incremental CFG construction updates for edited/changed functions.
- [x] Implement streaming MFG builder (no full-graph materialization requirement for large modules).
- [x] Add interprocedural RSS analysis pass manager.
- [x] **NEW:** Implement AST-based Metaprogramming engine (executes between MFG construction and SMIR emission).
- [x] **NEW:** Implement Effect System tracking (e.g., `[RSS_Mutate]`) to mathematically prove which functions modify RSS layout.
- [x] Integrate probabilistic profile ingestion into analysis.
- [x] Add hardware capability profile input to analysis (MMU/TLB/TBI/huge pages/trap behavior).
- [x] Define pass ordering constraints and invalidation rules.
- [x] Ensure deterministic pass output under fixed seed/profile inputs.

## 2. SMIR + Emission Framework

- [x] Define SMIR schema for all memory ops and state transitions.
- [x] Implement streaming SMIR emission from analysis results.
- [x] **NEW:** Expose SMIR/MFG query API to metaprogramming macros (e.g., enable `@RequireUnique(ptr)` to trigger compile-time assertions based on RSS state).
- [x] Add specialization metadata (path likelihood, assumptions, fallback link).
- [x] Inject fallback skeleton references at emission time.
- [x] Add SMIR verifier (well-formedness + invariants + fallback completeness).
- [x] Add SMIR printer/debug dumps and machine-readable snapshots.

## 3. Memory Operation ISA

- [x] Model all ops: `ALLOC FREE LOAD STORE MOVE COPY ALIAS RETAIN RELEASE ESCAPE CAPTURE PHI READ MUTATE CALL`.
- [x] Define legal preconditions/postconditions per op.
- [x] Add op lowering rules for hardware-accelerated path.
- [x] Add op lowering rules for MMU-only mode.
- [x] Add op lowering rules for pure software fallback mode.
- [x] Add op auditing hooks to validate op sequence semantics in debug builds.

## 4. Memory State Model + Lattice

- [x] Encode states: `UNIQUE LENT OWNED_COW RC_SHARED IMMUTABLE ESCAPED UNKNOWN`.
- [x] Implement lattice and monotonic refinement engine.
- [x] Enforce legal transitions only (reject/repair invalid edges).
- [x] **NEW:** Formalize and implement secure transition proofs for `RC_SHARED` -> `UNIQUE` demotion.
- [x] Implement `ESCAPED` as region-scoped sink with recovery rules.
- [x] Add join/widening strategy with bounded convergence guarantees.
- [x] Add state visualizer for debugging and traceability.

## 5. Alias System

- [x] Implement `AliasSet(x)` representation across intraprocedural + interprocedural contexts.
- [x] Add bounded summarization strategy for scalability.
- [x] Add differential alias updates for incremental recompilation.
- [x] Add alias precision metrics and budget knobs.
- [x] Add alias contradiction detection and fallback escalation policy.

## 6. Allocation + Linearity + Mutation Rules

- [x] Enforce `ALLOC -> UNIQUE`.
- [x] **NEW:** Implement strict Linearity Checker to ensure `UNIQUE` pointers are mathematically isolated from the Alias System.
- [x] Implement implicit MOVE inference when value is provably dead after assignment.
- [x] Implement direct mutation path for `UNIQUE/LENT` (zero-overhead fast path).
- [x] Implement `OWNED_COW` mutation via hardware/shadow-copy strategy.
- [x] Add consistency checks for state transitions under inlining and PHI merges.

## 7. RC_SHARED Engine (ALR + TLLB + PSF)

- [x] Implement thread-local lease buffers (TLLB) data structures.
- [x] 🌟 **NEW:** Add NUMA-awareness to TLLBs and ALR worker threads to minimize cross-socket interconnect latency during reclamation.
- [x] Implement asynchronous lease reclamation worker (ALR).
- [x] Implement batched global publication/merge protocol.
- [x] Implement pressure-sensitive flushing (PSF) thresholds and partial flush.
- [x] **NEW:** Tune aggressive PSF heuristics to prevent "Lease Lag" and avoid premature Tier 3 memory-pressure degradation.
- [x] Add memory-pressure callbacks and cold-path flush triggers.
- [x] Prove lock-free/low-contention properties of hot path behavior.
- [x] Add starvation/fairness safeguards for delayed reclamation.

## 8. Escape + Recovery

- [x] Implement `ESCAPE(ptr, region)` semantics and region ownership metadata.
- [x] Track region locality and eligibility for `RECLAIM`.
- [x] Add conservative fallback if region purity/locality cannot be proven.
- [x] Add diagnostics for non-reclaimable escape patterns.

## 9. Call-Site / Interprocedural Behavior

- [x] Implement per-function alias summary generation.
- [x] Detect and annotate pure/impure procedures.
- [x] Add purity clustering for specialization reuse.
- [x] Apply summary-based call effects unless stronger local proof exists.
- [x] Add recursive SCC handling with convergence bounds.

## 10. SAO (Segmented Alias Offsetting)

- [x] Define pointer-bit layout for state encoding (TBI-compatible paths + fallback encoding).
- [x] **NEW:** Encode `nen` (null) presence into high-bits (TBI) for zero-cost hardware-accelerated null checking.
- [x] Implement state transition ops as arithmetic/bitwise transforms.
- [x] Validate address-space safety and collision constraints.
- [x] Add software metadata fallback when high-bit tagging unavailable.
- [x] Add sanitizer mode to validate tag transitions at runtime.

## 11. SMS (Slab-Mapped Shadowing)

- [x] Implement 64MB slab allocator backend and metadata maps.
- [x] Implement per-slab RO/RW dual mapping.
- [x] Add slab lifecycle management (allocate, split, retire, recycle).
- [x] **NEW:** Implement "Tombstoned" slab states to defer physical reclamation of `FREE` operations until Speculative Replay transactions are fully committed.
- [x] Add VMA pressure controls and fragmentation heuristics.
- [x] Add slab diagnostics and leak reporting.

## 12. Huge-Page Shadowing

- [x] Add 2MB+ huge-page allocation path.
- [x] Add fallback to base pages when huge pages unavailable.
- [x] Tune TLB-locality and alias-coherence policies.
- [x] Add runtime telemetry for huge-page hit/miss and fallback rates.

## 13. VAS (Virtual Alias Shadowing)

- [x] Implement dual virtual mappings (RO/RW).
- [x] Wire MMU protection changes for mutation/commit windows.
- [x] **NEW:** Wire `nen` TBI bit-patterns to trigger VAS TRAPs, preventing CPU memory access on null prior to evaluation.
- [x] Ensure fast path has no software safety branch checks.
- [x] Implement trap handling path -> deterministic fallback.
- [x] Add VAS emulation mode in HIL when direct support is limited.

## 14. Atomic Hardware Handshake

- [x] Implement transition protocol: invalidate RW alias in local TLB before RO publish.
- [x] **NEW:** Implement robust cross-core TLB shootdown coordination to prevent memory corruption during RO/RW dual-mapped page transitions.
- [x] Define architecture-specific barriers/fences as required.
- [x] Add formalized handshake state machine.
- [x] Add race detectors/tests for publish/visibility correctness.

## 15. Probabilistic Specialization + PBS

- [x] Build probabilistic model for state likelihoods (e.g., P(UNIQUE)).
- [x] Emit PBS branch-stubs with dominant fast path + skeleton fallback.
- [x] Add confidence thresholds for specialization vs generic path.
- [x] Add model calibration and decay strategy over time.
- [x] Add deterministic fallback on mis-speculation without semantic drift.

## 16. Cost Model + Strategy Selection

- [x] Implement cost function: `CPU + Memory + Copy + Fault + Recovery`.
- [x] Add per-target weighting presets (desktop/server/mobile/embedded).
- [x] Select hot-path full specialization vs cold skeleton strategy.
- [x] Gate JIT optimization to hot fallback paths only.
- [x] Add explainability report: why each strategy was chosen.

## 17. Decision Pipeline Integration

- [x] Implement full pipeline stages.
- [x] Add stage-level caching and invalidation.
- [x] Add pipeline trace output for debugging regressions.

## 18. DEI (Dual-Entry Inlining)

- [x] Generate `Entry_Speculative` and `Entry_Safe` for each eligible function.
- [x] Add global entry table and dynamic switching mechanism.
- [x] Preserve ABI consistency between entries.
- [x] Add warmup and re-entry policies for stable toggling.

## 19. Speculative Concurrency (STRICT / SPECULATIVE / REPLAYABLE)

- [x] Add execution mode metadata per region/function.
- [x] Implement snapshot capture and rollback primitives.
- [x] Implement replay engine for REPLAYABLE segments.
- [x] **NEW:** Integrate Replay Engine with SMS Tombstoning to guarantee transactional safety of memory allocations/frees.
- [x] Add user-space recovery handler API (UMTI-like).
- [x] Add deterministic conflict resolution order.

## 20. Safety + Degradation (DEEM + HIL)

- [x] Implement degradation tiers (Tier 0 to Tier 3).
- [x] **NEW:** Utilize Effect System (`[RSS_Mutate]`) to mathematically guarantee DEEM equivalence, identifying exactly which functions require Tier 3 fallback skeletons.
- [x] Enforce DEEM: `Execution(D(S)) == Semantics(S)` for all D(S).
- [x] Implement HIL capability detection and feature downgrades.
- [x] Provide software emulation for VAS/SAO/ALR/traps when hardware absent.
- [x] Add explicit runtime report of active tier + disabled accelerations.

## 21. Formal Correctness + Invariants

- [x] Encode invariants (fast path zero checks, bounded convergence, etc.).
- [x] Add formal/automated checks for monotonic alias refinement.
- [x] Add convergence proofs/empirical convergence bounds for snapshots.
- [x] Add invariant checker pass in CI.

## 22. Runtime / OS Integration

- [x] Implement page protection manager abstraction.
- [x] Implement trap/signal/exception bridge to runtime fallback handlers.
- [x] Add platform adapters (Linux/macOS/Windows where relevant).
- [x] Add kernel capability probing and permission diagnostics.
- [x] Add safe behavior under restricted environments (containers, sandboxed runtimes).

## 23. JIT + Fallback Skeleton Runtime

- [x] Emit skeleton fallback bodies at compile time for all optimized paths.
- [x] Implement runtime counter collection for fallback hotness.
- [x] JIT-compile hot fallback fragments only.
- [x] Add JIT cache eviction and code invalidation policy.
- [x] Add deopt-to-safe transitions preserving full semantics.

## 24. Observability + Tooling

- [x] Add RSS trace mode (state transitions, alias joins, fallback causes).
- [x] 🌟 **NEW:** Create RSS-aware Debugger Plugin (GDB/LLDB adapter) to correctly resolve SAO tags and dual-mapped (RO/RW) virtual addresses for developers.
- [x] Add counters: trap rate, fallback rate, lease flush rate, reclaim lag.
- [x] Add perf dashboards for tier-specific behavior.
- [x] Add debug visualization for CFG/MFG/alias graphs/lattice transitions.
- [x] Add reproducible bug capture bundle.

## 25. Security + Hardening

- [x] Validate pointer-tag tampering resilience.
- [x] 🌟 **NEW:** Integrate SAO pointer tags with hardware Control Flow Integrity (CFI / ARM PAC) to prevent tag forging on function pointers.
- [x] Harden trap handlers against reentrancy/recursion hazards.
- [x] Add guardrails for malformed or adversarial profile inputs.
- [x] Add memory corruption fault-containment paths.
- [x] Add secure defaults when capability detection is uncertain.

*(Sections 26 to 29 remain standard testing, validation, and release criteria)*

---

### Answering questions regarding Metaprogramming Macros:

> **"Do you want the macros to be able to 'inspect' the current RSS/Memory state of a variable, or should they only operate on the syntax?"**

**They absolutely must be able to inspect the semantic RSS/Memory state.**
If macros only operated on the AST syntax, they would essentially just be text replacers (like C macros or standard Rust `macro_rules!`). Because your architecture relies on a highly dynamic **Memory Flow Graph (MFG)** and **Monotonic Refinement Engine**, a syntax-only macro has no idea if `ptr` is `UNIQUE`, `RC_SHARED`, or `ESCAPED`. 

By placing the metaprogramming expansion **after** the MFG construction (added to Section 1 & 2 above), your macros gain "Compiler Vision." A user can write `@RequireUnique(x)` and the macro can literally query the SMIR API: `if (Compiler.GetState(x) != STATE_UNIQUE) { Compiler.EmitError("Cannot pass shared pointer to unique-only subsystem"); }`. This gives developers a way to enforce your complex memory lattice at compile-time without waiting for a runtime VAS trap.