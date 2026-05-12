#include "CodeGen.h"
#include <llvm/IR/Function.h>
#include <llvm/IR/Verifier.h>
#include <functional>
#include <iostream>

namespace luv {

llvm::Value* CodeGen::visit(VarDecl& node) {
    llvm::Type* T = getType(node.type);
    llvm::Value* initVal = node.init ? node.init->accept(*this) : llvm::Constant::getNullValue(T);

    if (!builder.GetInsertBlock()) {
        if (auto* ipat = dynamic_cast<IdentifierPattern*>(node.pattern)) {
            llvm::Constant* cInit = llvm::dyn_cast<llvm::Constant>(initVal);
            if (!cInit) cInit = llvm::Constant::getNullValue(T);
            auto* g = new llvm::GlobalVariable(*module, T, node.isConst,
                llvm::GlobalValue::InternalLinkage, cInit, ipat->name);
            varSemanticTypes[ipat->name] = node.type;
            return lastValue = g;
        }
        return nullptr;
    }

    generatePatternDestructuring(initVal, node.pattern, node.isMutable, node.type);
    return lastValue = initVal;
}

llvm::Value* CodeGen::visit(Assignment& node) {
    llvm::Value* val = node.value->accept(*this);
    if (!val) return nullptr;

    for (auto* target : node.targets) {
        if (auto* varExpr = dynamic_cast<VarExpr*>(target)) {
            if (namedValues.count(varExpr->name)) {
                auto& info = namedValues[varExpr->name];
                builder.CreateStore(val, info.ptr);
            } else if (auto* g = module->getGlobalVariable(varExpr->name, true)) {
                builder.CreateStore(val, g);
            }
        }
    }
    return lastValue = val;
}

llvm::Value* CodeGen::visit(ReturnStmt& node) {
    llvm::Value* val = node.value ? node.value->accept(*this) : nullptr;
    return lastValue = val ? builder.CreateRet(val) : builder.CreateRetVoid();
}

llvm::Value* CodeGen::visit(FuncDecl& node) {
    std::vector<llvm::Type*> argTypes;
    for (const auto& p : node.params) argTypes.push_back(getType(p.type));
    
    llvm::FunctionType* FT = llvm::FunctionType::get(getType(node.returnType), argTypes, false);
    llvm::Function* F = module->getFunction(node.name);
    if (!F) {
        F = llvm::Function::Create(FT, llvm::Function::ExternalLinkage, node.name, *module);
    }

    if (node.body) {
        auto* oldBB = builder.GetInsertBlock();
        llvm::BasicBlock* BB = llvm::BasicBlock::Create(context, "entry", F);
        builder.SetInsertPoint(BB);
        auto oldNamedValues = namedValues;
        auto* oldReturnType = currentReturnType;
        currentReturnType = getType(node.returnType);

        for (size_t i = 0; i < node.params.size(); ++i) {
            llvm::Argument* arg = F->getArg(i);
            llvm::AllocaInst* alloca = builder.CreateAlloca(arg->getType(), nullptr, node.params[i].name);
            builder.CreateStore(arg, alloca);
            namedValues[node.params[i].name] = {alloca, arg->getType(), true};
        }

        node.body->accept(*this);
        
        if (!builder.GetInsertBlock()->getTerminator()) {
            if (currentReturnType->isVoidTy()) builder.CreateRetVoid();
            else builder.CreateRet(llvm::Constant::getNullValue(currentReturnType));
        }

        namedValues = oldNamedValues;
        currentReturnType = oldReturnType;
        if (oldBB) builder.SetInsertPoint(oldBB);
        else builder.ClearInsertionPoint();
    }
    return lastValue = F;
}

llvm::Value* CodeGen::visit(BreakStmt& node) {
    if (loopStack.empty()) return nullptr;
    return lastValue = builder.CreateBr(loopStack.back().exitBB);
}

llvm::Value* CodeGen::visit(ContinueStmt& node) {
    if (loopStack.empty()) return nullptr;
    return lastValue = builder.CreateBr(loopStack.back().continueBB);
}

llvm::Value* CodeGen::visit(ExprStmt& node) {
    return lastValue = node.expr ? node.expr->accept(*this) : nullptr;
}

llvm::Value* CodeGen::visit(ExternDecl& node) {
    std::vector<llvm::Type*> argTypes;
    for (const auto& p : node.params) argTypes.push_back(getType(p.type));
    llvm::FunctionType* FT = llvm::FunctionType::get(getType(node.returnType), argTypes, true);
    return lastValue = module->getOrInsertFunction(node.name, FT).getCallee();
}

} // namespace luv
