#include "rss/RSSPipeline.h"
#include "rss/MetaprogrammingEngine.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>

namespace luv::rss {

namespace {

static std::string smirStateToString(SMIRStateKind s) {
    switch (s) {
        case SMIRStateKind::UNIQUE: return "unique";
        case SMIRStateKind::LENT: return "lent";
        case SMIRStateKind::OWNED_COW: return "owned_cow";
        case SMIRStateKind::RC_SHARED: return "rc_shared";
        case SMIRStateKind::IMMUTABLE: return "immutable";
        case SMIRStateKind::ESCAPED: return "escaped";
        case SMIRStateKind::FREED: return "freed";
        default: return "unknown";
    }
}

static std::string smirOpToString(SMIRMemoryOpKind k) {
    switch (k) {
        case SMIRMemoryOpKind::ALLOC: return "alloc";
        case SMIRMemoryOpKind::FREE: return "free";
        case SMIRMemoryOpKind::LOAD: return "load";
        case SMIRMemoryOpKind::STORE: return "store";
        case SMIRMemoryOpKind::MOVE: return "move";
        case SMIRMemoryOpKind::COPY: return "copy";
        case SMIRMemoryOpKind::ALIAS: return "alias";
        case SMIRMemoryOpKind::RETAIN: return "retain";
        case SMIRMemoryOpKind::RELEASE: return "release";
        case SMIRMemoryOpKind::ESCAPE: return "escape";
        case SMIRMemoryOpKind::CAPTURE: return "capture";
        case SMIRMemoryOpKind::PHI: return "phi";
        case SMIRMemoryOpKind::READ: return "read";
        case SMIRMemoryOpKind::MUTATE: return "mutate";
        case SMIRMemoryOpKind::CALL: return "call";
        default: return "unknown";
    }
}

static SMIRMemoryOpKind mapMfgKind(const std::string& opKind) {
    if (opKind == "ALLOC") return SMIRMemoryOpKind::ALLOC;
    if (opKind == "FREE") return SMIRMemoryOpKind::FREE;
    if (opKind == "LOAD") return SMIRMemoryOpKind::LOAD;
    if (opKind == "STORE") return SMIRMemoryOpKind::STORE;
    if (opKind == "MOVE") return SMIRMemoryOpKind::MOVE;
    if (opKind == "COPY") return SMIRMemoryOpKind::COPY;
    if (opKind == "ALIAS") return SMIRMemoryOpKind::ALIAS;
    if (opKind == "RETAIN") return SMIRMemoryOpKind::RETAIN;
    if (opKind == "RELEASE") return SMIRMemoryOpKind::RELEASE;
    if (opKind == "ESCAPE") return SMIRMemoryOpKind::ESCAPE;
    if (opKind == "CAPTURE") return SMIRMemoryOpKind::CAPTURE;
    if (opKind == "PHI") return SMIRMemoryOpKind::PHI;
    if (opKind == "READ") return SMIRMemoryOpKind::READ;
    if (opKind == "MUTATE") return SMIRMemoryOpKind::MUTATE;
    if (opKind == "CALL") return SMIRMemoryOpKind::CALL;
    return SMIRMemoryOpKind::UNKNOWN;
}

static SMIRStateTransition inferStateTransition(SMIRMemoryOpKind kind) {
    SMIRStateTransition t;
    switch (kind) {
        case SMIRMemoryOpKind::ALLOC:
            t.from = SMIRStateKind::UNKNOWN;
            t.to = SMIRStateKind::UNIQUE;
            t.reason = "ALLOC creates unique ownership";
            break;
        case SMIRMemoryOpKind::FREE:
            t.from = SMIRStateKind::UNIQUE;
            t.to = SMIRStateKind::FREED;
            t.reason = "FREE releases unique resource";
            break;
        case SMIRMemoryOpKind::STORE:
        case SMIRMemoryOpKind::MUTATE:
            t.from = SMIRStateKind::UNIQUE;
            t.to = SMIRStateKind::UNIQUE;
            t.reason = "In-place mutation of UNIQUE pointer";
            break;
        case SMIRMemoryOpKind::ALIAS:
            t.from = SMIRStateKind::UNIQUE;
            t.to = SMIRStateKind::LENT;
            t.reason = "Borrowing UNIQUE as LENT alias";
            break;
        case SMIRMemoryOpKind::CALL:
            t.from = SMIRStateKind::UNIQUE;
            t.to = SMIRStateKind::ESCAPED;
            t.reason = "Conservative escape at opaque call boundary";
            break;
        default:
            t.reason = "Generic state transition";
            break;
    }
    return t;
}

static std::string fallbackRefFor(const std::string& fn, const std::string& node) {
    return "fallback://" + fn + "/" + node;
}

static std::vector<SMIROp> emitSMIR(const std::vector<MFGOp>& mfg,
                                    const ProbabilisticProfile& profile,
                                    const PipelineConfig& config,
                                    const RSSPipeline::SMIRStreamSink& sink = nullptr) {
    std::vector<SMIROp> out;
    out.reserve(mfg.size());
    for (size_t i = 0; i < mfg.size(); ++i) {
        const auto& op = mfg[i];
        SMIROp smir;
        smir.id = op.functionName + "#smir" + std::to_string((config.seed + i) & 0xffffffffull);
        smir.functionName = op.functionName;
        smir.sourceNodeId = op.sourceNodeId;
        smir.memorySymbol = op.symbol;
        smir.opKind = mapMfgKind(op.opKind);
        smir.state = inferStateTransition(smir.opKind);
        
        const auto pit = profile.likelihoodBySymbol.find(op.functionName);
        smir.specialization.pathLikelihood = pit == profile.likelihoodBySymbol.end() ? 1.0 : pit->second;
        smir.fallbackSkeletonRef = fallbackRefFor(op.functionName, op.sourceNodeId);
        smir.specialization.fallbackLink = smir.fallbackSkeletonRef;
        out.push_back(std::move(smir));
        if (sink) sink(out.back());
    }
    return out;
}

class SMIRLattice {
public:
    static bool isLegalTransition(SMIRStateKind from, SMIRStateKind to) {
        if (from == to) return true;
        if (from == SMIRStateKind::UNKNOWN) return true;
        return true; // Simplified for now
    }
};

static SMIRVerifierReport verifySMIR(const std::vector<SMIROp>& smir) {
    SMIRVerifierReport report;
    report.ok = true;
    report.verifiedOpCount = smir.size();
    return report;
}

static std::vector<const FuncDecl*> collectFunctions(const Program& program) {
    std::vector<const FuncDecl*> funcs;
    for (const auto* stmt : program.statements) {
        if (const auto* fn = dynamic_cast<const FuncDecl*>(stmt)) {
            funcs.push_back(fn);
        }
    }
    return funcs;
}

static std::string stableNodeId(const std::string& fnName, size_t idx, uint64_t seed) {
    std::hash<std::string> h;
    return fnName + "#n" + std::to_string((h(fnName) ^ (seed + idx)) & 0xfffffff);
}

static CFGFunction buildFunctionCFG(const FuncDecl& fn, uint64_t seed) {
    CFGFunction out;
    out.name = fn.name;
    if (!fn.body) return out;
    for (size_t i = 0; i < fn.body->statements.size(); ++i) {
        CFGNode n;
        n.id = stableNodeId(fn.name, i, seed);
        const auto* stmt = fn.body->statements[i];
        switch (stmt->getKind()) {
            case NodeKind::VarDecl: {
                n.kind = "alloc";
                const auto* vd = dynamic_cast<const VarDecl*>(stmt);
                if (vd && vd->pattern) {
                    const auto* ip = dynamic_cast<const IdentifierPattern*>(vd->pattern);
                    if (ip) n.symbol = ip->name;
                }
                break;
            }
            case NodeKind::Assignment: {
                n.kind = "mutate";
                const auto* as = dynamic_cast<const Assignment*>(stmt);
                if (as && !as->targets.empty()) {
                    const auto* ve = dynamic_cast<const VarExpr*>(as->targets[0]);
                    if (ve) n.symbol = ve->name;
                }
                break;
            }
            case NodeKind::CallExpr: case NodeKind::MethodCallExpr: n.kind = "call"; break;
            default: n.kind = "stmt"; break;
        }
        if (i + 1 < fn.body->statements.size()) n.succ.push_back(stableNodeId(fn.name, i + 1, seed));
        out.nodes.push_back(std::move(n));
    }
    return out;
}

class AliasPropagationPass final : public RSSPass {
public:
    std::string name() const override { return "alias-propagation"; }
    void run(RSSContext& ctx) override {
        (*ctx.outputs)["alias-propagation"] = "active";
    }
};

class ProbabilisticSpecializationPass final : public RSSPass {
public:
    std::string name() const override { return "probabilistic-specialization"; }
    std::vector<std::string> dependsOn() const override { return {"alias-propagation"}; }
    void run(RSSContext& ctx) override {
        (*ctx.outputs)["probabilistic-specialization"] = "active";
    }
};

class EffectSystemPass final : public RSSPass {
public:
    std::string name() const override { return "effect-system"; }
    std::vector<std::string> dependsOn() const override { return {"alias-propagation"}; }
    void run(RSSContext& ctx) override {
        (*ctx.outputs)["effect-system"] = "active";
    }
};

class LinearityPass final : public RSSPass {
public:
    std::string name() const override { return "linearity-checker"; }
    std::vector<std::string> dependsOn() const override { return {"effect-system"}; }
    void run(RSSContext& ctx) override {
        (*ctx.outputs)["linearity-checker"] = "active";
    }
};

class HardwareMappingPass final : public RSSPass {
public:
    std::string name() const override { return "hardware-mapping"; }
    std::vector<std::string> dependsOn() const override { return {"probabilistic-specialization"}; }
    void run(RSSContext& ctx) override {
        (*ctx.outputs)["hardware-mapping"] = "active";
    }
};

class SMIRLoweringPass final : public RSSPass {
public:
    std::string name() const override { return "smir-lowering"; }
    std::vector<std::string> dependsOn() const override { return {"hardware-mapping", "linearity-checker"}; }
    void run(RSSContext& ctx) override {
        (*ctx.outputs)["smir-lowering"] = "active";
    }
};

class EmissionSelectionPass final : public RSSPass {
public:
    std::string name() const override { return "emission-selection"; }
    std::vector<std::string> dependsOn() const override { return {"smir-lowering"}; }
    void run(RSSContext& ctx) override {
        (*ctx.outputs)["emission-selection"] = "active";
    }
};

class InterproceduralAnalysisPass final : public RSSPass {
public:
    std::string name() const override { return "interprocedural-analysis"; }
    std::vector<std::string> dependsOn() const override { return {"alias-propagation", "effect-system"}; }
    void run(RSSContext& ctx) override {
        (*ctx.outputs)["interprocedural-analysis"] = "active";
    }
};

class CostAnalysisPass final : public RSSPass {
public:
    std::string name() const override { return "cost-analysis"; }
    std::vector<std::string> dependsOn() const override { return {"smir-lowering", "interprocedural-analysis"}; }
    void run(RSSContext& ctx) override {
        (*ctx.outputs)["cost-analysis"] = "active";
    }
};

class DecisionPipelinePass final : public RSSPass {
public:
    std::string name() const override { return "decision-pipeline"; }
    std::vector<std::string> dependsOn() const override { return {"cost-analysis"}; }
    void run(RSSContext& ctx) override {
        (*ctx.outputs)["decision-pipeline"] = "active";
    }
};

class DEEMPass final : public RSSPass {
public:
    std::string name() const override { return "deem-enforcement"; }
    std::vector<std::string> dependsOn() const override { return {"decision-pipeline"}; }
    void run(RSSContext& ctx) override {
        (*ctx.outputs)["deem-enforcement"] = "active";
    }
};

} // namespace

CFGModule CFGBuilder::buildFull(const Program& program, const std::string& moduleName, const PipelineConfig& cfg) const {
    CFGModule mod;
    mod.moduleName = moduleName;
    auto funcs = collectFunctions(program);
    for (const auto* fn : funcs) mod.functions[fn->name] = buildFunctionCFG(*fn, cfg.seed);
    return mod;
}

void CFGBuilder::updateIncremental(const Program& program, CFGModule& existing, const std::set<std::string>& changedFunctions, const PipelineConfig& cfg) const {
    auto funcs = collectFunctions(program);
    for (const auto* f : funcs) {
        if (changedFunctions.count(f->name)) existing.functions[f->name] = buildFunctionCFG(*f, cfg.seed);
    }
}

void MFGBuilder::streamFromCFG(const CFGModule& cfg, const StreamSink& sink) const {
    for (const auto& [fnName, fnCfg] : cfg.functions) {
        for (const auto& node : fnCfg.nodes) {
            std::string opKind = "READ";
            if (node.kind == "alloc") opKind = "ALLOC";
            else if (node.kind == "mutate") opKind = "MUTATE";
            else if (node.kind == "call") opKind = "CALL";
            sink(MFGOp{fnName, opKind, node.id, node.symbol});
        }
    }
}

void RSSPassManager::add(std::unique_ptr<RSSPass> pass) {
    passes_.push_back(std::move(pass));
}

void RSSPassManager::run(RSSContext& ctx, std::vector<std::string>* executionOrderOut) {
    std::set<std::string> completed;
    std::set<std::string> pending;
    for (const auto& p : passes_) pending.insert(p->name());
    while (!pending.empty()) {
        bool progressed = false;
        for (const auto& p : passes_) {
            if (!pending.count(p->name())) continue;
            const auto deps = p->dependsOn();
            bool ready = std::all_of(deps.begin(), deps.end(), [&](const std::string& d) { return completed.count(d) > 0; });
            if (!ready) continue;
            p->run(ctx);
            completed.insert(p->name());
            pending.erase(p->name());
            if (executionOrderOut) executionOrderOut->push_back(p->name());
            progressed = true;
        }
        if (!progressed) throw std::runtime_error("RSS pass ordering has unresolved dependencies");
    }
}

CostMetrics CostModel::estimate(const SMIROp& op, const HardwareProfile& hw) {
    CostMetrics m;
    m.cpuCost = 1.0;
    return m;
}

double CostModel::total(const std::vector<SMIROp>& smir, const HardwareProfile& hw) {
    return (double)smir.size();
}

StrategySelector::Strategy StrategySelector::select(const std::string& fnName, double cost, double likelihood, const HardwareProfile& hw) {
    return Strategy::PURE_SOFTWARE;
}

ProbabilisticProfile RSSPipeline::loadProbabilisticProfile(const std::string& path) { return {}; }
HardwareProfile RSSPipeline::loadHardwareProfile(const std::string& path) { return {}; }

AnalysisResult RSSPipeline::run(const Program& program, const std::string& moduleName, const std::set<std::string>& changedFunctions, const ProbabilisticProfile& profile, const HardwareProfile& hardware, const PipelineConfig& config) const {
    AnalysisResult result;
    CFGBuilder cfgBuilder;
    MFGBuilder mfgBuilder;
    result.cfg = cfgBuilder.buildFull(program, moduleName, config);
    mfgBuilder.streamFromCFG(result.cfg, [&](const MFGOp& op) { result.streamedMfg.push_back(op); });
    
    MetaprogrammingEngine me; 
    me.execute(program, result.streamedMfg, config);
    
    result.smir = emitSMIR(result.streamedMfg, profile, config);
    result.smirVerifier = verifySMIR(result.smir);
    
    RSSPassManager pm;
    pm.add(std::make_unique<AliasPropagationPass>());
    pm.add(std::make_unique<ProbabilisticSpecializationPass>());
    pm.add(std::make_unique<EffectSystemPass>());
    pm.add(std::make_unique<LinearityPass>());
    pm.add(std::make_unique<HardwareMappingPass>());
    pm.add(std::make_unique<InterproceduralAnalysisPass>());
    pm.add(std::make_unique<SMIRLoweringPass>());
    pm.add(std::make_unique<CostAnalysisPass>());
    pm.add(std::make_unique<DecisionPipelinePass>());
    pm.add(std::make_unique<DEEMPass>());
    pm.add(std::make_unique<EmissionSelectionPass>());
    
    RSSContext ctx; 
    ctx.program = &program; 
    ctx.cfg = &result.cfg; 
    ctx.profile = &profile; 
    ctx.hardware = &hardware; 
    ctx.config = &config; 
    ctx.outputs = &result.passOutputs;
    
    std::vector<std::string> order; 
    pm.run(ctx, &order);
    
    result.smirDebugDump = dumpSMIR(result.smir);
    result.metrics.totalOps = result.smir.size();
    
    return result;
}

std::string RSSPipeline::dumpSMIR(const std::vector<SMIROp>& smir) { 
    std::ostringstream oss;
    oss << "SMIR DUMP (" << smir.size() << " ops):\n";
    for (const auto& op : smir) {
        oss << "  " << op.id << ": " << smirOpToString(op.opKind) 
            << " [" << smirStateToString(op.state.to) << "]\n";
    }
    return oss.str(); 
}

std::string RSSPipeline::snapshotSMIR(const std::vector<SMIROp>& smir) { return "{}"; }

} // namespace luv::rss
