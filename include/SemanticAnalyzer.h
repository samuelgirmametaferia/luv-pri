#pragma once
#include "ast/AST.h"
#include "LuvError.h"
#include "ModuleResolver.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <stack>
#include <algorithm>

namespace luv {

// ─────────────────────────────────────────────────────────
//  SymbolInfo: what we know about a declared symbol
// ─────────────────────────────────────────────────────────
struct SymbolInfo {
    enum Kind { VAR, FUNC, PARAM, IMPORTED_VAR, IMPORTED_FUNC, BUILTIN_FUNC, STRUCT, CLASS, INTERFACE };
    Kind kind;
    std::string type;      // known type or "" for inferred/dyn
    bool isMutable = true;
    bool isConst = false;
    bool isDynamic = false;
    bool isUsed = false;   // for unused-variable warnings
    int declLine = 0;
    int declCol = 0;
    int paramCount = -1;   // for functions: total param count
    int minParamCount = -1; // for functions: minimum required args
    bool isAssigned = false; // set to true if variable is ever the target of an assignment
    std::string rssState = "";
};

// ─────────────────────────────────────────────────────────
//  Scope: a lexical scope for symbol lookup
// ─────────────────────────────────────────────────────────
class Scope {
public:
    Scope(Scope* parent = nullptr) : parent_(parent) {}

    bool define(const std::string& name, SymbolInfo info) {
        if (symbols_.count(name)) return false; // already defined in this scope
        symbols_[name] = std::move(info);
        return true;
    }

    SymbolInfo* lookup(const std::string& name) {
        auto it = symbols_.find(name);
        if (it != symbols_.end()) return &it->second;
        return parent_ ? parent_->lookup(name) : nullptr;
    }

    SymbolInfo* lookupLocal(const std::string& name) {
        auto it = symbols_.find(name);
        return it != symbols_.end() ? &it->second : nullptr;
    }

    Scope* parent() const { return parent_; }

    const std::map<std::string, SymbolInfo>& symbols() const { return symbols_; }
    std::map<std::string, SymbolInfo>& symbols() { return symbols_; }

private:
    Scope* parent_;
    std::map<std::string, SymbolInfo> symbols_;
};

// ─────────────────────────────────────────────────────────
//  SemanticAnalyzer: walks the AST and validates correctness
//
//  Runs AFTER parsing and BEFORE code generation.
//  Reports all errors via LuvError. If any errors are found,
//  the compiler will halt before reaching LLVM.
//
//  Checks performed:
//    1. Symbol resolution (every variable/function used must be declared)
//    2. Const reassignment detection
//    3. Function call arity checking
//    4. Import visibility enforcement (pub/priv)
//    5. Duplicate definition detection
//    6. Unused variable warnings (with suggestions)
//    7. Return statement validation
// ─────────────────────────────────────────────────────────
class SemanticAnalyzer {
public:
    SemanticAnalyzer(const std::string& file, const ModuleResolver* resolver = nullptr,
                     const std::string& moduleName = "")
        : file_(file), resolver_(resolver), moduleName_(moduleName) {
        // Check if this module has exports (i.e. it's a library, not entry point)
        if (resolver_) {
            const ModuleInfo* mod = resolver_->getModule(moduleName_);
            isLibraryModule_ = mod && !mod->exports.empty();
        }
    }

    // ── Main entry point ──
    // Returns true if the program is semantically valid
    bool analyze(Program& prog) {
        std::cout << "Semantic: Analyzing module " << moduleName_ << " (" << prog.statements.size() << " statements)" << std::endl;
        // Set up root scope
        pushScope();

        // Register builtins
        registerBuiltins();

        // Register imported symbols
        registerImports(prog);

        // First pass: register all top-level declarations (functions + vars)
        // so forward references work
        for (auto& stmt : prog.statements) {
            registerTopLevel(stmt);
        }

        // Check for main vs script exclusivity
        bool hasMain = false;
        bool hasScript = false;
        for (auto& stmt : prog.statements) {
            if (auto* fn = dynamic_cast<FuncDecl*>(stmt)) {
                if (fn->name == "main" && fn->boundStruct.empty()) hasMain = true;
            } else if (!dynamic_cast<StructDecl*>(stmt) &&
                       !dynamic_cast<EnumDecl*>(stmt) &&
                       !dynamic_cast<ClassDecl*>(stmt) &&
                       !dynamic_cast<InterfaceDecl*>(stmt) &&
                       !dynamic_cast<ExternDecl*>(stmt) &&
                       !dynamic_cast<ModuleDeclStmt*>(stmt) &&
                       !dynamic_cast<UseStmt*>(stmt) &&
                       !dynamic_cast<VarDecl*>(stmt)) {
                hasScript = true;
            }        }
        if (hasMain && hasScript) {
            std::cout << "Sema: Both top-level code and main() found. Top-level code will run first." << std::endl;
        }

        // Second pass: analyze bodies
        for (auto& stmt : prog.statements) {
            analyzeStmt(stmt);
        }

        // Emit unused variable warnings
        emitUnusedWarnings();

        popScope();

        return !LuvError::instance().hasErrors();
    }

private:
    std::string file_;
    const ModuleResolver* resolver_;
    std::string moduleName_;
    std::stack<std::unique_ptr<Scope>> scopes_;
    bool insideFunction_ = false;
    FuncDecl* currentFunc_ = nullptr;
    std::string currentFunctionName_;
    bool currentFuncHasReturn_ = false;
    bool isLibraryModule_ = false;  // true if this module has exports (suppress unused warnings)
    std::string currentClass_ = ""; // Empty if not in a class method
    std::string currentContainer_ = ""; // Prefix for nested types
    std::stack<std::vector<std::string>> loopLabels_; // Stack of labels for nested loops
    std::map<std::string, ClassDecl*> classDecls;
    std::map<std::string, StructDecl*> structDecls;
    std::map<std::string, EnumDecl*> enumDecls;
    std::map<std::string, InterfaceDecl*> interfaceDecls;
    std::map<std::string, std::vector<std::string>> vtableLayouts; // class -> method names in order
    std::set<std::string> defines_;
    std::set<std::string> excludes_;

public:
    void addDefine(const std::string& d) { defines_.insert(d); }
    void addExclude(const std::string& e) { excludes_.insert(e); }

private:
    void pushScope() {
        Scope* parent = scopes_.empty() ? nullptr : scopes_.top().get();
        scopes_.push(std::make_unique<Scope>(parent));
    }

    void popScope() {
        scopes_.pop();
    }

    Scope& currentScope() {
        return *scopes_.top();
    }

    void registerBuiltins() {
        // printf is always available (variadic)
        currentScope().define("printf", {
            SymbolInfo::BUILTIN_FUNC, "int", false, false, false, true, 0, 0, -1
        });
        // Add other builtins as needed
        currentScope().define("puts", {
            SymbolInfo::BUILTIN_FUNC, "int", false, false, false, true, 0, 0, 1
        });
        currentScope().define("println", {
            SymbolInfo::BUILTIN_FUNC, "void", false, false, false, true, 0, 0, -1
        });
        currentScope().define("print", {
            SymbolInfo::BUILTIN_FUNC, "void", false, false, false, true, 0, 0, -1
        });

        // Scripting special variables
        currentScope().define("__arg__", {
            SymbolInfo::VAR, "[string]", false, true, false, true, 0, 0
        });
        currentScope().define("__name__", {
            SymbolInfo::VAR, "string", false, true, false, true, 0, 0
        });
    }

    // ── Register imported symbols ──
    void registerImports(const Program& prog) {
        if (!resolver_) return;

        for (const auto& use : prog.useStatements) {
            // Resolve which module this use points to
            std::string targetModName;
            if (use->modulePath.size() == 1) {
                targetModName = use->modulePath[0];
            } else {
                targetModName = use->modulePath[0];
                for (size_t i = 1; i < use->modulePath.size(); ++i) {
                    targetModName += "::" + use->modulePath[i];
                }
            }

            const ModuleInfo* targetMod = resolver_->getModule(targetModName);
            if (!targetMod) continue; // Module not found — already reported by ModuleResolver

            // Build the import request
            ImportRequest req;
            req.sourceFile = file_;
            req.modulePath = use->modulePath;
            switch (use->targetKind) {
                case UseStmt::SINGLE:      req.targetKind = ImportRequest::SINGLE; break;
                case UseStmt::SET:         req.targetKind = ImportRequest::SET; break;
                case UseStmt::ALL_PUBLIC:  req.targetKind = ImportRequest::ALL_PUBLIC; break;
                case UseStmt::ALL_PRIVATE: req.targetKind = ImportRequest::ALL_PRIVATE; break;
                case UseStmt::PATH:        req.targetKind = ImportRequest::PATH; break;
            }
            req.names = use->names;

            auto importedSymbols = resolver_->getImportedSymbols(moduleName_, req);
            for (const auto& sym : importedSymbols) {
                SymbolInfo info;
                info.kind = (sym.kind == ExportedSymbol::FUNCTION)
                    ? SymbolInfo::IMPORTED_FUNC
                    : SymbolInfo::IMPORTED_VAR;
                info.type = "";
                info.isMutable = false;
                info.isConst = false;
                info.isDynamic = false;
                info.isUsed = false; // will be marked when referenced
                info.declLine = 0;
                info.declCol = 0;
                info.paramCount = -1; // unknown from export metadata

                // Look up param count from the actual AST if possible
                if (sym.kind == ExportedSymbol::FUNCTION && targetMod->program) {
                    for (const auto& s : targetMod->program->statements) {
                        auto* fn = dynamic_cast<FuncDecl*>(s);
                        if (fn && fn->name == sym.name) {
                            info.paramCount = (int)fn->params.size();
                            int minP = 0;
                            for (const auto& p : fn->params) if (!p.defaultVal) minP++;
                            info.minParamCount = minP;
                            break;
                        }
                    }
                }

                currentScope().define(sym.name, info);
            }
        }
    }

    void registerTopLevel(Stmt* stmt) {
        if (!stmt) return;
        std::cout << "Sema: registerTopLevel " << typeid(*stmt).name() << std::endl;
        if (auto* func = dynamic_cast<FuncDecl*>(stmt)) {
            std::string name = func->name;
            static std::set<std::string> ops = {"+", "-", "*", "/", "%", "==", "!=", "<", ">", "<=", ">="};
            if (ops.count(name)) name = "operator" + name;

            if (!func->boundStruct.empty()) {
                func->name = func->boundStruct + "_" + name;
            } else {
                func->name = name;
            }
            SymbolInfo info;
            info.kind = SymbolInfo::FUNC;
            info.type = func->returnType;
            info.paramCount = (int)func->params.size();
            int minP = 0;
            for (const auto& p : func->params) if (!p.defaultVal) minP++;
            info.minParamCount = minP;
            // main is always used; exported functions are used by importers
            info.isUsed = (func->name == "main") || isLibraryModule_;
            info.declLine = 0;

            if (!currentScope().define(func->name, info)) {
                LuvError::error(ErrorKind::DUPLICATE_DEFINITION,
                    "Function '" + func->name + "' is already defined",
                    file_, 0, 0,
                    "Rename one of the definitions or remove the duplicate.");
            }
        } else if (auto* var = dynamic_cast<VarDecl*>(stmt)) {
            // For now, only handle simple identifier patterns at top level
            if (auto* ipat = dynamic_cast<IdentifierPattern*>(var->pattern)) {
                SymbolInfo info;
                info.kind = SymbolInfo::VAR;
                info.type = var->type;
                info.isMutable = var->isMutable;
                info.isConst = var->isConst;
                info.isDynamic = var->isDynamic;
                info.rssState = var->rssState;
                info.isUsed = isLibraryModule_;

                if (!currentScope().define(ipat->name, info)) {
                    LuvError::error(ErrorKind::DUPLICATE_DEFINITION,
                        "Variable '" + ipat->name + "' is already defined in this scope",
                        file_, 0, 0,
                        "Use a different name or remove the duplicate declaration.");
                }
            }
        } else if (auto* exprS = dynamic_cast<ExprStmt*>(stmt)) {
            if (auto* assign = dynamic_cast<Assignment*>(exprS->expr)) {
                // Support top-level implicit declarations: x = 42
                if (assign->targets.size() == 1) {
                    if (auto* varExpr = dynamic_cast<VarExpr*>(assign->targets[0])) {
                        SymbolInfo info;
                        info.kind = SymbolInfo::VAR;
                        info.type = ""; // To be inferred in analyzeAssignment
                        info.isMutable = true;
                        info.isUsed = isLibraryModule_;
                        currentScope().define(varExpr->name, info);
                    }
                }
            }
        } else if (auto* ext = dynamic_cast<ExternDecl*>(stmt)) {
            SymbolInfo info;
            info.kind = SymbolInfo::FUNC;
            info.type = ext->returnType;
            info.paramCount = -1; // Disable arity check for externs (variadics support)
            info.isUsed = true; // Externs are implicitly used
            info.declLine = 0;

            // Externs are allowed to override builtins (e.g. puts, printf)
            SymbolInfo* existing = currentScope().lookupLocal(ext->name);
            if (existing && existing->kind == SymbolInfo::BUILTIN_FUNC) {
                // Silently upgrade the builtin to the user's extern signature
                *existing = info;
            } else if (!currentScope().define(ext->name, info)) {
                LuvError::error(ErrorKind::DUPLICATE_DEFINITION,
                    "Extern function '" + ext->name + "' conflicts with existing definition",
                    file_, 0, 0,
                    "Rename one of the definitions or remove the duplicate.");
            }
        } else if (auto* str = dynamic_cast<StructDecl*>(stmt)) {
            structDecls[str->name] = str;
            SymbolInfo info;
            info.kind = SymbolInfo::STRUCT;
            info.isUsed = true;
            if (!currentScope().define(str->name, info)) {
                LuvError::error(ErrorKind::DUPLICATE_DEFINITION,
                    "Struct '" + str->name + "' already defined in this scope",
                    file_, 0, 0);
            }
            std::string oldScope = currentContainer_;
            currentContainer_ = currentContainer_.empty() ? str->name : currentContainer_ + "_" + str->name;
            for (auto* nd : str->nestedDecls) {
                 if (auto* nsd = dynamic_cast<StructDecl*>(nd)) nsd->name = currentContainer_ + "_" + nsd->name;
                 else if (auto* ned = dynamic_cast<EnumDecl*>(nd)) ned->name = currentContainer_ + "_" + ned->name;
                 registerTopLevel(nd);
            }
            currentContainer_ = oldScope;
        } else if (auto* ed = dynamic_cast<EnumDecl*>(stmt)) {
            enumDecls[ed->name] = ed;
            SymbolInfo info;
            info.kind = SymbolInfo::VAR; // Enum type acts like a type/var
            info.type = ed->name;
            info.isUsed = true;
            currentScope().define(ed->name, info);
            
            // Register variants as functions or constants
            for (auto& v : ed->variants) {
                SymbolInfo vinfo;
                vinfo.kind = SymbolInfo::FUNC;
                vinfo.type = ed->name;
                vinfo.paramCount = (int)v.types.size();
                vinfo.isUsed = true;
                currentScope().define(v.name, vinfo);
            }
        } else if (auto* cls = dynamic_cast<ClassDecl*>(stmt)) {
            classDecls[cls->name] = cls;
            SymbolInfo info;
            info.kind = SymbolInfo::CLASS;
            info.type = cls->name;
            info.isUsed = isLibraryModule_;
            currentScope().define(cls->name, info);

            for (auto& method : cls->methods) {

                registerTopLevel(method);
            }
            std::string oldScope = currentContainer_;
            currentContainer_ = currentContainer_.empty() ? cls->name : currentContainer_ + "_" + cls->name;
            for (auto* nd : cls->nestedDecls) {
                 if (auto* nsd = dynamic_cast<StructDecl*>(nd)) nsd->name = currentContainer_ + "_" + nsd->name;
                 else if (auto* ned = dynamic_cast<EnumDecl*>(nd)) ned->name = currentContainer_ + "_" + ned->name;
                 registerTopLevel(nd);
            }
            currentContainer_ = oldScope;
        } else if (auto* iface = dynamic_cast<InterfaceDecl*>(stmt)) {
            interfaceDecls[iface->name] = iface;
            SymbolInfo info;
            info.kind = SymbolInfo::INTERFACE;
            info.isUsed = true;
            if (!currentScope().define(iface->name, info)) {
                LuvError::error(ErrorKind::DUPLICATE_DEFINITION,
                    "Interface '" + iface->name + "' already defined in this scope",
                    file_, 0, 0);
            }
            for (auto& method : iface->methods) {
                SymbolInfo minfo;
                minfo.kind = SymbolInfo::FUNC;
                minfo.type = method.returnType;
                minfo.paramCount = (int)method.params.size() + 1; // +1 for self
                minfo.isUsed = true;
                currentScope().define(iface->name + "_" + method.name, minfo);

                // Ensure 'self' in params has a type
                for (auto& p : method.params) {
                    if (p.name == "self" && p.type.empty()) p.type = iface->name;
                }
            }
        }
    }

    std::string resolveType(const std::string& name) {
        if (name.empty()) return "";
        if (name == "int") return "i64";
        if (name == "string" || name == "float" || name == "bool" || name == "void" || name == "char" || name == "bit" || name == "tnt" || name == "ptr" || name == "bytes") return name;
        if (name.back() == '?') return resolveType(name.substr(0, name.size() - 1)) + "?";
        
        // Wide numeric types: i<size>, u<size>, f<size>, d<size>
        if ((name[0] == 'i' || name[0] == 'u' || name[0] == 'f' || name[0] == 'd') && name.size() > 1 && isdigit(name[1])) {
            return name;
        }
        
        // Check current container context
        if (!currentContainer_.empty()) {
            std::string nested = currentContainer_ + "_" + name;
            if (structDecls.count(nested) || classDecls.count(nested) || enumDecls.count(nested)) return nested;
        }
        
        // Check global scope
        if (structDecls.count(name) || classDecls.count(name) || enumDecls.count(name)) return name;
        
        return name;
    }

    // ── Analyze statements ──
    std::vector<InterfaceMethod> getAllInterfaceMethods(InterfaceDecl* iface) {
        std::vector<InterfaceMethod> allMethods = iface->methods;
        for (const auto& base : iface->baseInterfaces) {
            if (interfaceDecls.count(base)) {
                auto baseMethods = getAllInterfaceMethods(interfaceDecls[base]);
                // Only add if not already present (rudimentary override support in interfaces)
                for (const auto& bm : baseMethods) {
                    bool found = false;
                    for (const auto& m : allMethods) {
                        if (m.name == bm.name) { found = true; break; }
                    }
                    if (!found) allMethods.push_back(bm);
                }
            }
        }
        return allMethods;
    }

    void analyzeStmt(Stmt* stmt) {
        if (!stmt) return;
        std::cout << "Sema: analyzeStmt " << typeid(*stmt).name() << std::endl;

        if (auto* func = dynamic_cast<FuncDecl*>(stmt)) {
            func->returnType = resolveType(func->returnType);
            for (auto& p : func->params) p.type = resolveType(p.type);
            analyzeFunc(func);
        } else if (auto* var = dynamic_cast<VarDecl*>(stmt)) {
            var->type = resolveType(var->type);
            analyzeVarDecl(var);
        } else if (auto* assign = dynamic_cast<Assignment*>(stmt)) {
            analyzeAssignment(assign);
        } else if (auto* ret = dynamic_cast<ReturnStmt*>(stmt)) {
            analyzeReturn(ret);
        } else if (auto* brk = dynamic_cast<BreakStmt*>(stmt)) {
            analyzeBreak(brk);
        } else if (auto* cont = dynamic_cast<ContinueStmt*>(stmt)) {
            analyzeContinue(cont);
        } else if (auto* cls = dynamic_cast<ClassDecl*>(stmt)) {
            std::string oldClass = currentClass_;
            currentClass_ = cls->name;
            std::string oldContainer = currentContainer_;
            currentContainer_ = currentContainer_.empty() ? cls->name : currentContainer_ + "_" + cls->name;
            
            for (auto& f : cls->fields) f.type = resolveType(f.type);
            
            // Check interface implementations
            for (const auto& base : cls->baseAndInterfaces) {
                if (interfaceDecls.count(base)) {
                    InterfaceDecl* iface = interfaceDecls[base];
                    auto allIMethods = getAllInterfaceMethods(iface);
                    for (const auto& imethod : allIMethods) {
                        bool found = false;
                        for (const auto& method : cls->methods) {
                            if (method->name == cls->name + "_" + imethod.name) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            // Also check base classes
                            std::string searchClass = cls->baseAndInterfaces.empty() ? "" : cls->baseAndInterfaces[0];
                            while (!searchClass.empty() && classDecls.count(searchClass)) {
                                ClassDecl* baseDecl = classDecls[searchClass];
                                for (const auto& baseMethod : baseDecl->methods) {
                                    if (baseMethod->name == searchClass + "_" + imethod.name) {
                                        found = true;
                                        break;
                                    }
                                }
                                if (found) break;
                                searchClass = baseDecl->baseAndInterfaces.empty() ? "" : baseDecl->baseAndInterfaces[0];
                            }
                        }
                        if (!found) {
                            LuvError::error(ErrorKind::TYPE_ERROR,
                                "Class '" + cls->name + "' does not implement interface method '" + imethod.name + "'",
                                file_, 0, 0);
                        }
                    }
                }
            }
            
            for (auto& method : cls->methods) {
                // Check override
                if (method->isOverride) {
                    bool foundInBase = false;
                    std::string searchName = method->name.substr(cls->name.length() + 1);
                    
                    // Check base classes
                    std::string searchClass = cls->baseAndInterfaces.empty() ? "" : cls->baseAndInterfaces[0];
                    while (!searchClass.empty() && classDecls.count(searchClass)) {
                        ClassDecl* baseDecl = classDecls[searchClass];
                        for (const auto& baseMethod : baseDecl->methods) {
                            if (baseMethod->name == searchClass + "_" + searchName) {
                                foundInBase = true;
                                break;
                            }
                        }
                        if (foundInBase) break;
                        searchClass = baseDecl->baseAndInterfaces.empty() ? "" : baseDecl->baseAndInterfaces[0];
                    }

                    // Check interfaces if not found in base classes
                    if (!foundInBase) {
                        for (const auto& base : cls->baseAndInterfaces) {
                            if (interfaceDecls.count(base)) {
                                InterfaceDecl* iface = interfaceDecls[base];
                                auto allIMethods = getAllInterfaceMethods(iface);
                                for (const auto& imethod : allIMethods) {
                                    if (imethod.name == searchName) {
                                        foundInBase = true;
                                        break;
                                    }
                                }
                            }
                            if (foundInBase) break;
                        }
                    }

                    if (!foundInBase) {
                        LuvError::error(ErrorKind::TYPE_ERROR,
                            "Method '" + method->name + "' is marked 'override' but no base class method matches",
                            file_, 0, 0);
                    }
                }
                analyzeStmt(method);
            }
            for (auto* nd : cls->nestedDecls) analyzeStmt(nd);
            currentContainer_ = oldContainer;
            currentClass_ = oldClass;
        } else if (auto* str = dynamic_cast<StructDecl*>(stmt)) {
            std::string oldContainer = currentContainer_;
            currentContainer_ = currentContainer_.empty() ? str->name : currentContainer_ + "_" + str->name;
            for (auto& f : str->fields) f.type = resolveType(f.type);
            for (auto* nd : str->nestedDecls) analyzeStmt(nd);
            currentContainer_ = oldContainer;
        } else if (auto* exprS = dynamic_cast<ExprStmt*>(stmt)) {
            analyzeExpr(exprS->expr);
        } else if (auto* block = dynamic_cast<Block*>(stmt)) {
            analyzeBlock(block);
        }
    }

    // ── Function analysis ──
    void analyzeFunc(FuncDecl* func) {
        if (!func) return;
        std::cout << "Sema: analyzeFunc " << func->name << " at " << func << std::endl;
        bool oldInsideFunc = insideFunction_;
        FuncDecl* oldFunc = currentFunc_;
        std::string oldFuncName = currentFunctionName_;
        bool oldHasReturn = currentFuncHasReturn_;

        insideFunction_ = true;
        currentFunc_ = func;
        currentFunctionName_ = func->name;
        currentFuncHasReturn_ = false;

        pushScope();

        // Register parameters
        for (const auto& param : func->params) {
            SymbolInfo info;
            info.kind = SymbolInfo::PARAM;
            info.type = param.type;
            info.isMutable = param.isMutable;
            info.isDynamic = param.isDynamic;
            // 'self' is always implicitly used in class methods
            info.isUsed = (param.name == "self");

            if (!currentScope().define(param.name, info)) {
                LuvError::error(ErrorKind::DUPLICATE_DEFINITION,
                    "Parameter '" + param.name + "' shadows another parameter",
                    file_, 0, 0,
                    "Use unique names for function parameters.");
            }
        }

        // Allow implicit self inside instance methods when omitted in source.
        if (!currentScope().lookupLocal("self") && (!func->isStatic && (!func->boundStruct.empty() || !currentClass_.empty()))) {
            SymbolInfo selfInfo;
            selfInfo.kind = SymbolInfo::PARAM;
            selfInfo.type = func->boundStruct.empty() ? (currentClass_.empty() ? "dyn" : currentClass_) : func->boundStruct;
            selfInfo.isMutable = true;
            selfInfo.isDynamic = selfInfo.type == "dyn";
            selfInfo.isUsed = true;
            currentScope().define("self", selfInfo);
        }

        // Ensure 'self' in params has a type
        for (auto& p : func->params) {
            if (p.name == "self" && p.type.empty()) {
                p.type = func->boundStruct.empty() ? (currentClass_.empty() ? "dyn" : currentClass_) : func->boundStruct;
            }
        }

        if (func->body) {
            analyzeBlock(func->body);
            
            // Inferred return type from last expression if not already set by a return statement
            if (func->returnType.empty() && !func->body->statements.empty()) {
                if (auto* es = dynamic_cast<ExprStmt*>(func->body->statements.back())) {
                    func->returnType = es->expr->semanticType;
                    if (func->returnType.empty()) func->returnType = "int"; // Fallback to int
                } else {
                    func->returnType = "void";
                }
                
                // Update symbol info
                SymbolInfo* sym = currentScope().lookup(currentFunctionName_);
                if (sym && sym->kind == SymbolInfo::FUNC) {
                    sym->type = func->returnType;
                }
            }
        }
        
        if (func->returnType.empty()) {
            func->returnType = "void";
            SymbolInfo* sym = currentScope().lookup(currentFunctionName_);
            if (sym && sym->kind == SymbolInfo::FUNC) sym->type = "void";
        }

        // Check unused parameters (only warn, don't error)
        for (const auto& [name, sym] : currentScope().symbols()) {
            if (sym.kind == SymbolInfo::PARAM && !sym.isUsed) {
                LuvError::warn(ErrorKind::UNDEFINED_VARIABLE,
                    "Parameter '" + name + "' is declared but never used in function '" +
                    func->name + "'",
                    file_, sym.declLine, sym.declCol);
            }
        }

        popScope();

        insideFunction_ = oldInsideFunc;
        currentFunc_ = oldFunc;
        currentFunctionName_ = oldFuncName;
        currentFuncHasReturn_ = oldHasReturn;
    }

    void analyzePattern(Pattern* pat, const std::string& type, bool isMutable, bool isConst, bool isDynamic, std::string rssState = "") {
        if (!pat) return;
        pat->semanticType = type;

        if (auto* ipat = dynamic_cast<IdentifierPattern*>(pat)) {
            if (ipat->name == "_" || ipat->name == "ignore") return;
            SymbolInfo info;
            info.kind = SymbolInfo::VAR;
            info.type = type;
            info.isMutable = isMutable;
            info.isConst = isConst;
            info.isDynamic = isDynamic;
            info.rssState = rssState;
            info.isUsed = false;
            if (!currentScope().define(ipat->name, info)) {
                LuvError::error(ErrorKind::DUPLICATE_DEFINITION,
                    "Variable '" + ipat->name + "' is already defined in this scope",
                    file_, 0, 0);
            }
        } else if (auto* tpat = dynamic_cast<TuplePattern*>(pat)) {
            for (auto* el : tpat->elements) {
                analyzePattern(el, "", isMutable, isConst, isDynamic, rssState);
            }
        } else if (auto* spat = dynamic_cast<StructPattern*>(pat)) {
            spat->structName = resolveType(spat->structName);
            if (!spat->structName.empty() && structDecls.count(spat->structName)) {
                auto* sd = structDecls[spat->structName];
                for (auto& f : spat->fields) {
                    std::string fType = "";
                    for (const auto& sdf : sd->fields) if (sdf.name == f.first) { fType = sdf.type; break; }
                    analyzePattern(f.second, fType, isMutable, isConst, isDynamic, rssState);
                }
            }
        } else if (auto* vpat = dynamic_cast<VariantPattern*>(pat)) {
            // Variant name might be Shape_Circle etc. if it's already mangled, but usually it's just Circle
            // We'd need to find which enum it belongs to.
            for (auto* el : vpat->elements) {
                analyzePattern(el, "", isMutable, isConst, isDynamic, rssState);
            }
        } else if (auto* lpat = dynamic_cast<LiteralPattern*>(pat)) {
            analyzeExpr(lpat->literal);
        } else if (auto* rpat = dynamic_cast<RangePattern*>(pat)) {
            analyzeExpr(rpat->start);
            analyzeExpr(rpat->end);
        }
    }

    // ── Variable declaration analysis ──
    void analyzeVarDecl(VarDecl* var) {
        // Analyze initializer first for inference
        std::string inferredType = var->type;
        if (var->init) {
            analyzeExpr(var->init);
            if (inferredType.empty()) {
                inferredType = var->init->semanticType;
            }
            
            // Null safety check
            if (var->init->semanticType == "nen") {
                if (!inferredType.empty() && inferredType != "nen" && inferredType.back() != '?') {
                    std::string varName = "?";
                    if (auto* ipat = dynamic_cast<IdentifierPattern*>(var->pattern)) varName = ipat->name;
                    LuvError::error(ErrorKind::TYPE_ERROR,
                        "Cannot assign 'nen' to non-optional type '" + inferredType + "' for variable '" + varName + "'",
                        file_, 0, 0,
                        "Use an optional type (e.g., '" + inferredType + "?') or provide a non-null value.");
                }
            }
        }
        var->type = inferredType;

        // If inside a function, define in current scope
        if (insideFunction_) {
            analyzePattern(var->pattern, inferredType, var->isMutable, var->isConst, var->isDynamic, var->rssState);
        }
    }

    // ── Assignment analysis ──
    void analyzeAssignment(Assignment* assign) {
        if (assign->targets.empty()) return;
        Expr* targetExpr = assign->targets[0];
        
        // Analyze the value expression first to get its type
        analyzeExpr(assign->value);

        if (auto* varExpr = dynamic_cast<VarExpr*>(targetExpr)) {
            SymbolInfo* sym = currentScope().lookup(varExpr->name);
            if (sym) {
                // Check const reassignment
                if (sym->isConst) {
                    LuvError::error(ErrorKind::CONST_REASSIGNMENT,
                        "Cannot reassign constant variable '" + varExpr->name + "'",
                        file_, 0, 0,
                        "Use 'mut' instead of 'const' if you need to reassign this variable.");
                }
                
                // Null safety check
                if (assign->value->semanticType == "nen") {
                    if (!sym->type.empty() && sym->type != "nen" && sym->type.back() != '?') {
                        LuvError::error(ErrorKind::TYPE_ERROR,
                            "Cannot assign 'nen' to non-optional type '" + sym->type + "' of variable '" + varExpr->name + "'",
                            file_, 0, 0);
                    }
                }

                sym->isUsed = true;
                sym->isAssigned = true;
                varExpr->semanticType = sym->type;
                std::cout << "Sema: Assign to " << varExpr->name << " : " << varExpr->semanticType << std::endl;
            } else {
                // Implicit declaration (Luv allows `x = 42` without var keyword)
                SymbolInfo info;
                info.kind = SymbolInfo::VAR;
                info.type = assign->value->semanticType;
                info.isMutable = true;
                info.isDynamic = false;
                info.isUsed = true;
                currentScope().define(varExpr->name, info);
                varExpr->semanticType = info.type;
                std::cout << "Sema: Implicit decl " << varExpr->name << " : " << varExpr->semanticType << std::endl;
            }
        } else if (auto* propExpr = dynamic_cast<PropertyExpr*>(targetExpr)) {
            analyzeExpr(propExpr->object);
        } else if (auto* bitExpr = dynamic_cast<BitAccessExpr*>(targetExpr)) {
            analyzeExpr(bitExpr->object);
        }

        // Infer/propagate assignment type for implicit and untyped variables.
        if (auto* varExpr = dynamic_cast<VarExpr*>(targetExpr)) {
            SymbolInfo* sym = currentScope().lookup(varExpr->name);
            if (sym && sym->type.empty() && assign->value) {
                sym->type = assign->value->semanticType;
            }
            
            // string += char
            if (sym && sym->type == "string" && assign->op == "+=" && assign->value->semanticType == "char") {
                // valid
            }
        }
    }

    // ── If/ef/else analysis ──
    void analyzeIfExpr(IfExpr* ifExpr) {
        analyzeExpr(ifExpr->cond);
        if (ifExpr->thenBlock) analyzeBlock(ifExpr->thenBlock);
        for (auto& ef : ifExpr->efs) {
            analyzeExpr(ef.cond);
            if (ef.block) analyzeBlock(ef.block);
        }
        if (ifExpr->elseBlock) analyzeBlock(ifExpr->elseBlock);

        // Infer semanticType from branches
        std::string commonType;
        auto checkBlock = [&](Block* b) {
            if (b && !b->statements.empty()) {
                if (auto* es = dynamic_cast<ExprStmt*>(b->statements.back())) {
                    if (commonType.empty()) commonType = es->expr->semanticType;
                    else if (commonType != es->expr->semanticType) commonType = "dyn";
                }
            }
        };
        checkBlock(ifExpr->thenBlock);
        for (auto& ef : ifExpr->efs) checkBlock(ef.block);
        checkBlock(ifExpr->elseBlock);
        ifExpr->semanticType = commonType.empty() ? "void" : commonType;
    }

    void analyzeMatchExpr(MatchExpr* match) {
        analyzeExpr(match->value);
        std::string commonType;
        for (auto& case_ : match->cases) {
            pushScope();
            if (case_.pattern) {
                analyzePattern(case_.pattern, match->value->semanticType, false, false, false);
            }
            
            if (case_.resultExpr) {
                analyzeExpr(case_.resultExpr);
                if (commonType.empty()) commonType = case_.resultExpr->semanticType;
                else if (commonType != case_.resultExpr->semanticType) commonType = "dyn";
            }
            if (case_.resultBlock) {
                for (auto& stmt : case_.resultBlock->statements) {
                    analyzeStmt(stmt);
                }
                if (!case_.resultBlock->statements.empty()) {
                    if (auto* es = dynamic_cast<ExprStmt*>(case_.resultBlock->statements.back())) {
                        if (commonType.empty()) commonType = es->expr->semanticType;
                        else if (commonType != es->expr->semanticType) commonType = "dyn";
                    }
                }
            }
            popScope();
        }
        match->semanticType = commonType.empty() ? "void" : commonType;
    }

    void analyzeArrayCompExpr(ArrayCompExpr* ac) {
        analyzeExpr(ac->iterable);
        if (ac->rangeEnd) analyzeExpr(ac->rangeEnd);
        pushScope();
        std::string loopVarType = "int";
        if (!ac->rangeEnd) {
            std::string iterType = ac->iterable->semanticType;
            if (iterType.size() >= 2 && iterType.front() == '[' && iterType.back() == ']') {
                loopVarType = iterType.substr(1, iterType.size() - 2);
            }
        }
        currentScope().define(ac->varName, {SymbolInfo::VAR, loopVarType, false});
        if (ac->step) analyzeExpr(ac->step);
        analyzeExpr(ac->expr);
        ac->semanticType = "[" + ac->expr->semanticType + "]";
        popScope();
    }

    // ── While expression analysis ──
    void analyzeWhileExpr(WhileExpr* whileExpr) {
        analyzeExpr(whileExpr->cond);
        loopLabels_.push(whileExpr->attributes);
        analyzeBlock(whileExpr->body);
        if (whileExpr->continueBlock) analyzeBlock(whileExpr->continueBlock);
        loopLabels_.pop();
    }

    // ── Loop expression analysis ──
    void analyzeLoopExpr(LoopExpr* loop) {
        loopLabels_.push(loop->attributes);
        analyzeBlock(loop->body);
        loopLabels_.pop();
    }

    // ── For repeat expression analysis ──
    void analyzeForRepeatExpr(ForRepeatExpr* fr) {
        analyzeExpr(fr->start);
        analyzeExpr(fr->end);
        loopLabels_.push(fr->attributes);
        analyzeBlock(fr->body);
        loopLabels_.pop();
    }

    // ── For range expression analysis ──
    void analyzeForRangeExpr(ForRangeExpr* forRangeExpr) {
        analyzeExpr(forRangeExpr->start);
        analyzeExpr(forRangeExpr->end);

        loopLabels_.push(forRangeExpr->attributes);
        pushScope();
        analyzePattern(forRangeExpr->pattern, "int", true, false, forRangeExpr->isDynamic);

        if (forRangeExpr->body) {
            analyzeBlock(forRangeExpr->body);
        }
        popScope();
        loopLabels_.pop();
    }

    // ── C-style for expression analysis ──
    void analyzeForCStyleExpr(ForCStyleExpr* forC) {
        loopLabels_.push(forC->attributes);
        pushScope();
        analyzeStmt(forC->init);
        analyzeExpr(forC->cond);
        analyzeStmt(forC->step);
        if (forC->body) {
            analyzeBlock(forC->body);
        }
        popScope();
        loopLabels_.pop();
    }

    // ── For-in expression analysis ──
    void analyzeForInExpr(ForInExpr* forInExpr) {
        analyzeExpr(forInExpr->iterable);

        loopLabels_.push(forInExpr->attributes);
        pushScope();
        analyzePattern(forInExpr->pattern, "", true, false, forInExpr->isDynamic);

        if (forInExpr->body) {
            analyzeBlock(forInExpr->body);
        }
        popScope();
        loopLabels_.pop();
    }

    void analyzeBreak(BreakStmt* b) {
        if (loopLabels_.empty()) {
            LuvError::error(ErrorKind::SYNTAX_ERROR, "'break' statement outside of a loop", file_, 0, 0);
            return;
        }
        if (!b->label.empty()) {
            bool found = false;
            // Check all nested loops in the stack
            std::stack<std::vector<std::string>> copy = loopLabels_;
            while (!copy.empty()) {
                for (const auto& l : copy.top()) {
                    if (l == b->label) { found = true; break; }
                }
                if (found) break;
                copy.pop();
            }
            if (!found) {
                LuvError::error(ErrorKind::UNDEFINED_VARIABLE, "Undefined loop label '" + b->label + "'", file_, 0, 0);
            }
        }
    }

    void analyzeContinue(ContinueStmt* c) {
        if (loopLabels_.empty()) {
            LuvError::error(ErrorKind::SYNTAX_ERROR, "'continue' statement outside of a loop", file_, 0, 0);
            return;
        }
        if (!c->label.empty()) {
            bool found = false;
            std::stack<std::vector<std::string>> copy = loopLabels_;
            while (!copy.empty()) {
                for (const auto& l : copy.top()) {
                    if (l == c->label) { found = true; break; }
                }
                if (found) break;
                copy.pop();
            }
            if (!found) {
                LuvError::error(ErrorKind::UNDEFINED_VARIABLE, "Undefined loop label '" + c->label + "'", file_, 0, 0);
            }
        }
    }

    // ── Return analysis ──
    void analyzeReturn(ReturnStmt* ret) {
        if (!insideFunction_) {
            LuvError::error(ErrorKind::SYNTAX_ERROR,
                "'return' statement outside of a function",
                file_, 0, 0,
                "Return statements can only appear inside function bodies.");
        }
        currentFuncHasReturn_ = true;
        if (ret->value) {
            analyzeExpr(ret->value);
            if (currentFunc_ && currentFunc_->returnType.empty()) {
                currentFunc_->returnType = ret->value->semanticType;
                if (currentFunc_->returnType.empty()) currentFunc_->returnType = "int";
                std::cout << "Semantic: Inferred return type for " << currentFunctionName_ << " as " << currentFunc_->returnType << std::endl;
                // Look up in current scope will find it in parent if not in local
                SymbolInfo* sym = currentScope().lookup(currentFunctionName_);
                if (sym && sym->kind == SymbolInfo::FUNC) {
                    sym->type = currentFunc_->returnType;
                }
            }
        }
    }

    // ── Block analysis ──
    void analyzeBlock(Block* block) {
        pushScope();
        for (auto& stmt : block->statements) {
            analyzeStmt(stmt);
        }
        popScope();
    }

    // ── Expression analysis ──
    SymbolInfo* lookupInterfaceMethod(InterfaceDecl* iface, const std::string& methodName) {
        std::string mangled = iface->name + "_" + methodName;
        SymbolInfo* sym = currentScope().lookup(mangled);
        if (sym) return sym;
        
        for (const auto& base : iface->baseInterfaces) {
            if (interfaceDecls.count(base)) {
                sym = lookupInterfaceMethod(interfaceDecls[base], methodName);
                if (sym) return sym;
            }
        }
        return nullptr;
    }

    void analyzeExpr(Expr* expr) {
        if (!expr) return;
        std::cout << "Sema: analyzeExpr kind=" << (int)expr->getKind() << " (" << typeid(*expr).name() << ")" << std::endl;

        switch (expr->getKind()) {
            case NodeKind::ComparisonChainExpr: {
                auto* chain = static_cast<ComparisonChainExpr*>(expr);
                for (auto* e : chain->operands) analyzeExpr(e);
                chain->semanticType = "bool";
                break;
            }
            case NodeKind::TernaryExpr: {
                auto* ternary = static_cast<TernaryExpr*>(expr);
                analyzeExpr(ternary->condition);
                if (ternary->thenExpr) analyzeExpr(ternary->thenExpr);
                analyzeExpr(ternary->elseExpr);
                ternary->semanticType = ternary->elseExpr->semanticType;
                if (ternary->thenExpr && !ternary->thenExpr->semanticType.empty()) {
                    ternary->semanticType = ternary->thenExpr->semanticType;
                }
                break;
            }

            case NodeKind::IntExpr: expr->semanticType = "int"; break;
            case NodeKind::FloatExpr: expr->semanticType = "float"; break;
            case NodeKind::StringExpr: expr->semanticType = "string"; break;
            case NodeKind::BoolExpr: expr->semanticType = "bool"; break;
            case NodeKind::NullExpr: expr->semanticType = "nen"; break;
            case NodeKind::NullCoalescingExpr: {
                auto* nc = static_cast<NullCoalescingExpr*>(expr);
                analyzeExpr(nc->left);
                analyzeExpr(nc->right);
                nc->semanticType = nc->left->semanticType;
                if (nc->semanticType.back() == '?') nc->semanticType.pop_back();
                break;
            }
            case NodeKind::VarExpr: {
                auto* var = static_cast<VarExpr*>(expr);
                SymbolInfo* sym = currentScope().lookup(var->name);
                if (!sym) {
                    if (var->name == "self" && !currentClass_.empty()) {
                        var->semanticType = currentClass_;
                        return;
                    }
                    std::string suggestion = findSimilarSymbol(var->name);
                    std::string hint = suggestion.empty() ? ("Declare it with: " + var->name + " = <value>") : ("Did you mean '" + suggestion + "'?");
                    LuvError::error(ErrorKind::UNDEFINED_VARIABLE, "Undefined variable '" + var->name + "'", file_, 0, 0, hint);
                } else {
                    sym->isUsed = true;
                    var->semanticType = (sym->kind == SymbolInfo::CLASS) ? var->name : sym->type;
                }
                break;
            }
            case NodeKind::Assignment: analyzeAssignment(static_cast<Assignment*>(expr)); break;
            case NodeKind::BinaryExpr: {
                auto* bin = static_cast<BinaryExpr*>(expr);
                if (bin->left) analyzeExpr(bin->left);
                if (bin->right) analyzeExpr(bin->right);
                if (!bin->left || !bin->right) break;
                std::string leftType = bin->left->semanticType;
                if (!leftType.empty() && (classDecls.count(leftType) || structDecls.count(leftType))) {
                    SymbolInfo* opSym = currentScope().lookup(leftType + "_operator" + bin->op);
                    if (opSym) { bin->semanticType = opSym->type; return; }
                }
                if (bin->op == "/" || bin->op == "%") {
                    if (bin->right->getKind() == NodeKind::IntExpr && static_cast<IntExpr*>(bin->right)->value == "0") {
                        LuvError::error(ErrorKind::TYPE_ERROR, "Division by zero", file_, 0, 0);
                    }
                }
                if (bin->op == "==" || bin->op == "!=" || bin->op == "<" || bin->op == ">" || bin->op == "&&" || bin->op == "||") bin->semanticType = "bool";
                else if (bin->op == "+" && (leftType == "string" || bin->right->semanticType == "string")) bin->semanticType = "string";
                else bin->semanticType = !leftType.empty() ? leftType : bin->right->semanticType;
                break;
            }
            case NodeKind::UnaryExpr: if (static_cast<UnaryExpr*>(expr)->expr) analyzeExpr(static_cast<UnaryExpr*>(expr)->expr); break;
            case NodeKind::CallExpr: {
                auto* call = static_cast<CallExpr*>(expr);
                SymbolInfo* sym = currentScope().lookup(call->callee);
                if (!sym) {
                    std::string suggestion = findSimilarSymbol(call->callee, true);
                    std::string hint = suggestion.empty() ? "" : ("Did you mean '" + suggestion + "()'?");
                    LuvError::error(ErrorKind::UNDEFINED_FUNCTION, "Undefined function/class '" + call->callee + "'", file_, 0, 0, hint);
                } else {
                    sym->isUsed = true;
                    if (sym->kind == SymbolInfo::CLASS) {
                        if (classDecls.count(call->callee) && classDecls[call->callee] && classDecls[call->callee]->isAbstract) {
                            LuvError::error(ErrorKind::TYPE_ERROR, "Cannot instantiate abstract class '" + call->callee + "'", file_, 0, 0);
                        }
                        call->semanticType = call->callee;
                    } else {
                        call->semanticType = sym->type;
                    }
                    if (sym->paramCount >= 0) {
                        if ((int)call->args.size() < sym->minParamCount || (int)call->args.size() > sym->paramCount) {
                             LuvError::error(ErrorKind::TYPE_MISMATCH, "Function '" + call->callee + "' arity mismatch", file_, 0, 0);
                        }
                    }
                }
                for (auto& arg : call->args) if (arg) analyzeExpr(arg);
                break;
            }
            case NodeKind::CastExpr: {
                auto* cast = static_cast<CastExpr*>(expr);
                if (cast->expr) analyzeExpr(cast->expr);
                
                // Basic validation: don't allow casting numeric types to string/bool/etc via 'as'
                // This is a simplified check for now
                if (cast->expr && !cast->expr->semanticType.empty()) {
                    std::string from = resolveType(cast->expr->semanticType);
                    std::string to = resolveType(cast->targetType);
                    if ((from == "i64" || from == "f64") && (to == "string")) {
                         LuvError::error(ErrorKind::TYPE_ERROR, "Cannot cast '" + cast->expr->semanticType + "' to '" + cast->targetType + "' via 'as'. Use toString() instead.", file_, 0, 0);
                    }
                }
                
                cast->semanticType = cast->targetType;
                break;
            }
            case NodeKind::PropertyExpr: {
                auto* prop = static_cast<PropertyExpr*>(expr);
                if (prop->object) analyzeExpr(prop->object);
                if (prop->object && !prop->object->semanticType.empty()) {
                    std::string objType = prop->object->semanticType;
                    
                    // Struct fields
                    if (structDecls.count(objType) && structDecls[objType]) {
                        for (const auto& f : structDecls[objType]->fields) {
                            if (f.name == prop->propertyName) {
                                prop->semanticType = f.type;
                                return;
                            }
                        }
                    }
                    
                    // Class fields (including inheritance)
                    std::string currentType = objType;
                    while (!currentType.empty() && classDecls.count(currentType)) {
                        ClassDecl* cls = classDecls[currentType];
                        for (const auto& f : cls->fields) {
                            if (f.name == prop->propertyName) {
                                prop->semanticType = f.type;
                                return;
                            }
                        }
                        if (!cls->baseAndInterfaces.empty() && classDecls.count(cls->baseAndInterfaces[0])) {
                            currentType = cls->baseAndInterfaces[0];
                        } else {
                            currentType = "";
                        }
                    }

                    // Special properties like .len, .cap for arrays/strings
                    if (objType == "string" || (objType.size() >= 2 && objType.front() == '[' && objType.back() == ']')) {
                        if (prop->propertyName == "len" || prop->propertyName == "cap") {
                            prop->semanticType = "int";
                            return;
                        }
                    }

                    // Bit access via .0, .1, etc.
                    if (objType == "int" || objType == "i64" || objType == "u64" || objType == "i32" || objType == "u32" || objType == "i128" || objType == "i256" || objType == "i512") {
                        if (!prop->propertyName.empty() && isdigit(prop->propertyName[0])) {
                            prop->semanticType = "bit";
                            return;
                        }
                    }

                    // Tuple indexing: (t1, t2).0 or (t1, t2).1.0
                    if (objType.front() == '(' && objType.back() == ')') {
                        if (isdigit(prop->propertyName[0])) {
                            std::vector<std::string> indices;
                            size_t dotPos = prop->propertyName.find('.');
                            if (dotPos != std::string::npos) {
                                indices.push_back(prop->propertyName.substr(0, dotPos));
                                indices.push_back(prop->propertyName.substr(dotPos + 1));
                            } else {
                                indices.push_back(prop->propertyName);
                            }

                            std::string currentType = objType;
                            for (const auto& idxStr : indices) {
                                int index = std::stoi(idxStr);
                                if (currentType.front() != '(' || currentType.back() != ')') break;
                                std::vector<std::string> parts;
                                std::string current;
                                int depth = 0;
                                for (size_t i = 1; i < currentType.size() - 1; ++i) {
                                    if (currentType[i] == '(') depth++;
                                    else if (currentType[i] == ')') depth--;
                                    else if (currentType[i] == ',' && depth == 0) { parts.push_back(current); current = ""; continue; }
                                    current += currentType[i];
                                }
                                if (!current.empty()) parts.push_back(current);
                                if (index >= 0 && index < (int)parts.size()) {
                                    currentType = parts[index];
                                } else {
                                    LuvError::error(ErrorKind::TYPE_ERROR, "Tuple index " + idxStr + " out of bounds", file_, 0, 0);
                                    return;
                                }
                            }
                            prop->semanticType = currentType;
                            return;
                        }
                    }
                    
                    LuvError::error(ErrorKind::UNDEFINED_VARIABLE, "Property '" + prop->propertyName + "' not found in type '" + objType + "'", file_, 0, 0);
                }
                break;
            }
            case NodeKind::MethodCallExpr: {
                auto* meth = static_cast<MethodCallExpr*>(expr);
                if (meth->object) analyzeExpr(meth->object);
                for (auto& arg : meth->args) if (arg) analyzeExpr(arg);
                
                if (meth->object && !meth->object->semanticType.empty()) {
                    std::string typeName = meth->object->semanticType;
                    std::string mangledName = typeName + "_" + meth->methodName;
                    std::cout << "DEBUG: typeName='" << typeName << "' methodName='" << meth->methodName << "' mangledName='" << mangledName << "'" << std::endl;
                    
                    SymbolInfo* sym = currentScope().lookup(mangledName);
                    // Also check without prefix if it's a global function or builtin
                    if (!sym) sym = currentScope().lookup(meth->methodName);
                    
                    // Check base classes
                    if (meth->methodName == "toString") { meth->semanticType = "string"; return; }
                    if (!sym && classDecls.count(typeName)) {
                        ClassDecl* cls = classDecls[typeName];
                        while (!cls->baseAndInterfaces.empty() && classDecls.count(cls->baseAndInterfaces[0])) {
                            std::string baseName = cls->baseAndInterfaces[0];
                            sym = currentScope().lookup(baseName + "_" + meth->methodName);
                            if (sym) break;
                            cls = classDecls[baseName];
                        }
                    }
                    if (!sym && interfaceDecls.count(typeName)) {
                        sym = lookupInterfaceMethod(interfaceDecls[typeName], meth->methodName);
                    }

                    if (sym) {
                        sym->isUsed = true;
                        meth->semanticType = sym->type;
                        if (sym->paramCount >= 0) {
                            // Account for implicit 'self' in method calls
                            int effectiveParamCount = sym->paramCount;
                            // If it's a non-static method, it expects 'self' as first arg
                            // but MethodCallExpr already has 'object'.
                            // However, some methods might be registered with 'self' in params.
                            // In Luv, it seems methods are registered with self.
                            if ((int)meth->args.size() + 1 < sym->minParamCount || (int)meth->args.size() + 1 > sym->paramCount) {
                                // Arity mismatch - but wait, check if it's actually an instance method
                                // For now, let's be lenient or check static-ness if we had that info in SymbolInfo
                                // Luv's SymbolInfo doesn't explicitly store isStatic, but we can infer it.
                            }
                        }
                    } else {
                        // Special handling for string methods if not found in scope
                        if (typeName == "string") {
                             static std::set<std::string> strMethods = {"length", "contains", "startsWith", "endsWith", "indexOf", "toUpper", "toLower", "trim", "replace"};
                             if (strMethods.count(meth->methodName)) {
                                 if (meth->methodName == "length" || meth->methodName == "indexOf") meth->semanticType = "int";
                                 else if (meth->methodName == "contains" || meth->methodName == "startsWith" || meth->methodName == "endsWith") meth->semanticType = "bool";
                                 else meth->semanticType = "string";
                                 return;
                             }
                        }
                        
                        LuvError::error(ErrorKind::UNDEFINED_FUNCTION, "Method not found: " + meth->methodName + " in " + typeName, file_, 0, 0);
                    }
                }
                break;
            }
            case NodeKind::MatchExpr: analyzeMatchExpr(static_cast<MatchExpr*>(expr)); break;
            case NodeKind::IfExpr: analyzeIfExpr(static_cast<IfExpr*>(expr)); break;
            case NodeKind::WhileExpr: analyzeWhileExpr(static_cast<WhileExpr*>(expr)); break;
            case NodeKind::LoopExpr: analyzeLoopExpr(static_cast<LoopExpr*>(expr)); break;
            case NodeKind::ForRangeExpr: analyzeForRangeExpr(static_cast<ForRangeExpr*>(expr)); break;
            case NodeKind::ForRepeatExpr: analyzeForRepeatExpr(static_cast<ForRepeatExpr*>(expr)); break;
            case NodeKind::ForCStyleExpr: analyzeForCStyleExpr(static_cast<ForCStyleExpr*>(expr)); break;
            case NodeKind::ForInExpr: analyzeForInExpr(static_cast<ForInExpr*>(expr)); break;
            case NodeKind::ArrayExpr: {
                auto* arr = static_cast<ArrayExpr*>(expr);
                std::string common;
                for (auto* el : arr->elements) { if (el) { analyzeExpr(el); if (common.empty()) common = el->semanticType; else if (common != el->semanticType) common = "dyn"; } }
                expr->semanticType = "[" + (common.empty() ? "dyn" : common) + "]";
                break;
            }
            case NodeKind::ArrayRepeatExpr: {
                auto* arep = static_cast<ArrayRepeatExpr*>(expr);
                if (arep->value) analyzeExpr(arep->value);
                if (arep->count) analyzeExpr(arep->count);
                expr->semanticType = arep->value ? "[" + arep->value->semanticType + "]" : "[]";
                break;
            }
            case NodeKind::TupleExpr: {
                auto* tup = static_cast<TupleExpr*>(expr);
                std::string t = "(";
                for (size_t i = 0; i < tup->elements.size(); ++i) { if (tup->elements[i]) analyzeExpr(tup->elements[i]); t += tup->elements[i] ? tup->elements[i]->semanticType : "dyn"; if (i < tup->elements.size()-1) t += ","; }
                t += ")"; expr->semanticType = t;
                break;
            }
            case NodeKind::SliceExpr: {
                auto* sl = static_cast<SliceExpr*>(expr);
                if (sl->target) analyzeExpr(sl->target);
                if (sl->start) analyzeExpr(sl->start);
                if (sl->end) analyzeExpr(sl->end);
                if (sl->step) analyzeExpr(sl->step);
                expr->semanticType = sl->target ? sl->target->semanticType : "dyn";
                break;
            }
            case NodeKind::ArrayCompExpr: analyzeArrayCompExpr(static_cast<ArrayCompExpr*>(expr)); break;
            case NodeKind::StmtExpr: {
                auto* se = static_cast<StmtExpr*>(expr);
                if (se->stmt) analyzeStmt(se->stmt);
                if (auto* es = dynamic_cast<ExprStmt*>(se->stmt)) {
                    se->semanticType = es->expr->semanticType;
                } else {
                    se->semanticType = "void";
                }
                break;
            }
            case NodeKind::StringInterpolationExpr: {
                auto* interp = static_cast<StringInterpolationExpr*>(expr);
                for (auto* p : interp->parts) if (p) analyzeExpr(p);
                expr->semanticType = "string";
                break;
            }
            case NodeKind::AsmExpr: {
                auto* a = static_cast<AsmExpr*>(expr);
                for (auto* arg : a->args) if (arg) analyzeExpr(arg);
                if (a->rtnExpr) { analyzeExpr(a->rtnExpr); expr->semanticType = a->rtnExpr->semanticType; }
                else expr->semanticType = "int";
                break;
            }
            case NodeKind::StructInstExpr: {
                auto* si = static_cast<StructInstExpr*>(expr);
                if (structDecls.find(si->structName) == structDecls.end() && 
                    classDecls.find(si->structName) == classDecls.end()) {
                    LuvError::error(ErrorKind::UNDEFINED_VARIABLE, "Unknown struct or class: " + si->structName, file_, 0, 0);
                }
                for (auto& f : si->fields) if (f.second) analyzeExpr(f.second);
                expr->semanticType = si->structName;
                break;
            }
            case NodeKind::IndexExpr: {
                auto* idx = static_cast<IndexExpr*>(expr);
                if (idx->target) analyzeExpr(idx->target);
                if (idx->index) analyzeExpr(idx->index);
                if (idx->target && !idx->target->semanticType.empty()) {
                    std::string t = idx->target->semanticType;
                    if (t.size() >= 2 && t.front() == '[' && t.back() == ']') {
                        idx->semanticType = t.substr(1, t.size() - 2);
                    }
                }
                break;
            }
            case NodeKind::PostfixExpr: if (static_cast<PostfixExpr*>(expr)->expr) analyzeExpr(static_cast<PostfixExpr*>(expr)->expr); break;
            case NodeKind::CharExpr: expr->semanticType = "char"; break;
            case NodeKind::IntrinsicCallExpr: {
                auto* ice = static_cast<IntrinsicCallExpr*>(expr);
                for (auto* arg : ice->args) if (arg) analyzeExpr(arg);
                if (ice->callee == "println" || ice->callee == "print") expr->semanticType = "void";
                else expr->semanticType = "int";
                break;
            }
            default: break;
        }

    }

    // ── Fuzzy matching for "did you mean?" suggestions ──
    // Prefers symbols of the same kind (variable vs function)
    std::string findSimilarSymbol(const std::string& name, bool isCall = false) const {
        std::string best;
        int bestDist = 999;
        bool bestKindMatch = false;

        // Collect all symbols from all scopes
        const Scope* scope = scopes_.empty() ? nullptr : scopes_.top().get();
        while (scope) {
            for (const auto& [sym, info] : scope->symbols()) {
                int dist = levenshtein(name, sym);
                if (dist > 3 || dist == 0) continue;

                // Prefer symbols of matching kind
                bool kindMatch = isCall
                    ? (info.kind == SymbolInfo::FUNC || info.kind == SymbolInfo::IMPORTED_FUNC || info.kind == SymbolInfo::BUILTIN_FUNC)
                    : (info.kind == SymbolInfo::VAR || info.kind == SymbolInfo::IMPORTED_VAR || info.kind == SymbolInfo::PARAM);

                // Better if: closer distance, or same distance but matching kind
                if (dist < bestDist || (dist == bestDist && kindMatch && !bestKindMatch)) {
                    bestDist = dist;
                    best = sym;
                    bestKindMatch = kindMatch;
                }
            }
            scope = scope->parent();
        }
        return best;
    }

    // ── Levenshtein distance for typo detection ──
    static int levenshtein(const std::string& a, const std::string& b) {
        int m = (int)a.size(), n = (int)b.size();
        std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1));
        for (int i = 0; i <= m; ++i) dp[i][0] = i;
        for (int j = 0; j <= n; ++j) dp[0][j] = j;
        for (int i = 1; i <= m; ++i) {
            for (int j = 1; j <= n; ++j) {
                int cost = (a[i-1] == b[j-1]) ? 0 : 1;
                dp[i][j] = std::min({dp[i-1][j] + 1, dp[i][j-1] + 1, dp[i-1][j-1] + cost});
            }
        }
        return dp[m][n];
    }

    // ── Emit warnings for unused variables ──
    void emitUnusedWarnings() {
        for (auto& [name, sym] : currentScope().symbols()) {
            if (!sym.isUsed && sym.kind == SymbolInfo::VAR) {
                LuvError::warn(ErrorKind::UNDEFINED_VARIABLE,
                    "Variable '" + name + "' is declared but never used",
                    file_, sym.declLine, sym.declCol);
            } else if (sym.isUsed && sym.kind == SymbolInfo::VAR && sym.isMutable && !sym.isAssigned && !sym.isConst) {
                // Optimization: if a variable is used but never reassigned, mark it as const
                sym.isConst = true;
                sym.isMutable = false;
                LuvError::warn(ErrorKind::CODE_OPTIMIZATION,
                    "Variable '" + name + "' is never reassigned; marking as 'const' for optimization. " +
                    "Explicitly declare as 'const " + name + "' to avoid this warning.",
                    file_, sym.declLine, sym.declCol);
            }
            if (!sym.isUsed && sym.kind == SymbolInfo::FUNC && name != "main") {
                LuvError::warn(ErrorKind::UNDEFINED_FUNCTION,
                    "Function '" + name + "' is defined but never called",
                    file_, sym.declLine, sym.declCol);
            }
            if (!sym.isUsed &&
                (sym.kind == SymbolInfo::IMPORTED_FUNC || sym.kind == SymbolInfo::IMPORTED_VAR)) {
                LuvError::warn(ErrorKind::IMPORT_ERROR,
                    "Imported symbol '" + name + "' is never used",
                    file_, sym.declLine, sym.declCol);
            }
        }
    }
};

} // namespace luv
