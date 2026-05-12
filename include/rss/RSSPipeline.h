#pragma once

#include "ast/AST.h"
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace luv::rss {

struct CFGNode {
    std::string id;
    std::string kind;
    std::string symbol;
    std::vector<std::string> succ;
};

struct CFGFunction {
    std::string name;
    std::vector<CFGNode> nodes;
};

struct CFGModule {
    std::string moduleName;
    std::map<std::string, CFGFunction> functions;
};

struct MFGOp {
    std::string functionName;
    std::string opKind;
    std::string sourceNodeId;
    std::string symbol;
};

enum class SMIRMemoryOpKind {
    ALLOC,
    FREE,
    LOAD,
    STORE,
    MOVE,
    COPY,
    ALIAS,
    RETAIN,
    RELEASE,
    ESCAPE,
    CAPTURE,
    PHI,
    READ,
    MUTATE,
    CALL,
    UNKNOWN
};

enum class SMIRStateKind {
    UNKNOWN,
    UNIQUE,
    LENT,
    OWNED_COW,
    RC_SHARED,
    IMMUTABLE,
    ESCAPED,
    FREED
};

struct SMIRStateTransition {
    SMIRStateKind from = SMIRStateKind::UNKNOWN;
    SMIRStateKind to = SMIRStateKind::UNKNOWN;
    std::string reason;
};

struct SMIRSpecializationMetadata {
    double pathLikelihood = 1.0;
    std::vector<std::string> assumptions;
    std::string fallbackLink;
};

struct SMIROp {
    std::string id;
    std::string functionName;
    std::string sourceNodeId;
    std::string memorySymbol;
    SMIRMemoryOpKind opKind = SMIRMemoryOpKind::UNKNOWN;
    SMIRStateTransition state;
    SMIRSpecializationMetadata specialization;
    std::string fallbackSkeletonRef;

    // Section 19: Speculative metadata
    bool isSpeculative = false;
    std::string regionId;
};

struct SMIRVerifierIssue {
    enum class Severity { ERROR, WARNING };
    Severity severity = Severity::ERROR;
    std::string code;
    std::string message;
    std::string opId;
};

struct SMIRVerifierReport {
    bool ok = true;
    size_t verifiedOpCount = 0;
    std::vector<SMIRVerifierIssue> issues;
};

struct ProbabilisticProfile {
    std::map<std::string, double> likelihoodBySymbol;
};

struct HardwareProfile {
    bool hasMMU = true;
    bool hasTLBAlias = false;
    bool hasTBI = false;
    bool hasHugePages = false;
    bool hasTrapAssist = false;
};

struct PipelineConfig {
    uint64_t seed = 0x525353ull;
    bool deterministic = true;

    // Section 5: Alias knobs
    size_t aliasBudget = 1000;
    bool enableDifferentialAlias = true;
    double aliasPrecisionThreshold = 0.9;
};

struct AliasSet {
    std::set<std::string> symbols;
    bool isEscaped = false;
};

struct AliasSummary {
    std::map<std::string, AliasSet> sets;
    std::map<std::string, std::string> symbolToSet;

    // Section 5: Contradiction detection
    std::vector<std::string> contradictions;
    bool hasContradiction() const { return !contradictions.empty(); }
};

struct InterproceduralSummary {
    std::string functionName;
    AliasSummary aliasSummary;
    bool isPure = false;
    std::set<std::string> escapedSymbols;
    std::map<std::string, SMIRStateKind> returnState;
};

struct CostMetrics {
    double cpuCost = 0.0;
    double memoryCost = 0.0;
    double faultCost = 0.0;
    double recoveryCost = 0.0;
};

struct AnalysisResult {
    CFGModule cfg;
    std::vector<MFGOp> streamedMfg;
    std::vector<SMIROp> smir;
    SMIRVerifierReport smirVerifier;
    std::string smirDebugDump;
    std::string smirSnapshot;
    std::map<std::string, std::string> passOutputs;

    // Section 9: Interprocedural state
    std::map<std::string, InterproceduralSummary> summaries;

    // Section 0.5: Success Metrics
    struct Metrics {
        size_t totalOps = 0;
        size_t uniqueOps = 0;
        size_t sharedOps = 0;
        size_t escapedOps = 0;
        double avgPathLikelihood = 0.0;
        size_t verifierIssues = 0;
        double overallCost = 0.0;
    } metrics;
};

class CostModel {
public:
    static CostMetrics estimate(const SMIROp& op, const HardwareProfile& hw);
    static double total(const std::vector<SMIROp>& smir, const HardwareProfile& hw);
};

class StrategySelector {
public:
    enum class Strategy { FULL_SPECIALIZATION, SKELETON_ONLY, JIT_HYBRID, PURE_SOFTWARE };
    static Strategy select(const std::string& fnName, double cost, double likelihood, const HardwareProfile& hw);
};

class CFGBuilder {
public:
    CFGModule buildFull(const Program& program, const std::string& moduleName, const PipelineConfig& cfg) const;
    void updateIncremental(const Program& program,
                           CFGModule& existing,
                           const std::set<std::string>& changedFunctions,
                           const PipelineConfig& cfg) const;
};

class MFGBuilder {
public:
    using StreamSink = std::function<void(const MFGOp&)>;
    void streamFromCFG(const CFGModule& cfg, const StreamSink& sink) const;
};

struct RSSContext {
    const Program* program = nullptr;
    const CFGModule* cfg = nullptr;
    const ProbabilisticProfile* profile = nullptr;
    const HardwareProfile* hardware = nullptr;
    const PipelineConfig* config = nullptr;
    std::map<std::string, std::string>* outputs = nullptr;
};

class RSSPass {
public:
    virtual ~RSSPass() = default;
    virtual std::string name() const = 0;
    virtual std::vector<std::string> dependsOn() const { return {}; }
    virtual std::vector<std::string> invalidates() const { return {}; }
    virtual void run(RSSContext& ctx) = 0;
};

class RSSPassManager {
public:
    void add(std::unique_ptr<RSSPass> pass);
    void run(RSSContext& ctx, std::vector<std::string>* executionOrderOut = nullptr);

private:
    std::vector<std::unique_ptr<RSSPass>> passes_;
};

class RSSPipeline {
public:
    using SMIRStreamSink = std::function<void(const SMIROp&)>;

    static ProbabilisticProfile loadProbabilisticProfile(const std::string& path);
    static HardwareProfile loadHardwareProfile(const std::string& path);

    static std::string dumpSMIR(const std::vector<SMIROp>& smir);
    static std::string snapshotSMIR(const std::vector<SMIROp>& smir);

    AnalysisResult run(const Program& program,
                       const std::string& moduleName,
                       const std::set<std::string>& changedFunctions,
                       const ProbabilisticProfile& profile,
                       const HardwareProfile& hardware,
                       const PipelineConfig& config) const;
};

} // namespace luv::rss
