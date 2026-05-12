#pragma once
#include "LuvBaseVisitor.h"
#include "ast/AST.h"
#include "LuvError.h"
#include "Arena.h"
#include <memory>
#include <any>
#include <iostream>
#include <type_traits>

namespace luv {

class LuvVisitorImpl : public LuvBaseVisitor {
public:
    LuvVisitorImpl(Arena& arena) : arena_(arena) {}

    // Set current file for error reporting
    void setCurrentFile(const std::string& file) { currentFile_ = file; }

    template<typename T>
    T* cast_any(std::any a) {
        if (!a.has_value()) {
            // std::cerr << "cast_any: empty any" << std::endl;
            return nullptr;
        }
        
        // 1. Direct match
        if (a.type() == typeid(T*)) return std::any_cast<T*>(a);
        
        // 2. Polymorphic extraction via Node*
        if (a.type() == typeid(Node*)) {
            Node* node = std::any_cast<Node*>(a);
            if (node) return dynamic_cast<T*>(node);
        }
        
        // 3. Concrete matches
        if (a.type() == typeid(Expr*)) return dynamic_cast<T*>(std::any_cast<Expr*>(a));
        if (a.type() == typeid(Stmt*)) return dynamic_cast<T*>(std::any_cast<Stmt*>(a));
        
        return nullptr;
    }

    std::any visitUseFromStmt(LuvParser::UseFromStmtContext *ctx) override {
        UseStmt::TargetKind kind;
        std::vector<std::string> names;
        std::string targetText = ctx->useTarget()->getText();
        
        if (targetText == "*") {
            kind = UseStmt::ALL_PUBLIC;
        } else if (targetText == "@") {
            kind = UseStmt::ALL_PRIVATE;
        } else if (targetText.find('{') != std::string::npos) {
            kind = UseStmt::SET;
        } else {
            kind = UseStmt::SINGLE;
            names.push_back(targetText);
        }
        
        std::vector<std::string> modulePath;
        for (auto id : ctx->modulePath()->IDENTIFIER()) {
            modulePath.push_back(id->getText());
        }
        
        return (Node*)arena_.alloc<UseStmt>(kind, std::move(names), std::move(modulePath));
    }

    std::any visitUsePathStmt(LuvParser::UsePathStmtContext *ctx) override {
        std::vector<std::string> modulePath;
        for (auto id : ctx->modulePath()->IDENTIFIER()) {
            modulePath.push_back(id->getText());
        }
        std::string target = modulePath.back();
        modulePath.pop_back();
        
        return (Node*)arena_.alloc<UseStmt>(UseStmt::PATH, std::vector<std::string>{target}, std::move(modulePath));
    }

    std::any visitProgram(LuvParser::ProgramContext *ctx) override {
        std::string moduleName;
        std::vector<UseStmt*> useStmts;
        std::vector<Stmt*> statements;

        if (ctx->moduleDecl()) {
            auto modCtx = ctx->moduleDecl();
            auto pathCtx = modCtx->modulePath();
            std::vector<std::string> path;
            for (auto id : pathCtx->IDENTIFIER()) {
                path.push_back(id->getText());
            }
            moduleName = path[0];
            for (size_t i = 1; i < path.size(); ++i) {
                moduleName += "::" + path[i];
            }
        }

        for (auto topCtx : ctx->topLevel()) {
            if (topCtx->useStmt()) {
                auto result = visit(topCtx->useStmt());
                if (result.has_value()) {
                    useStmts.push_back(cast_any<UseStmt>(result));
                }
            } else if (topCtx->visibilityDecl()) {
                auto result = visit(topCtx->visibilityDecl());
                if (result.has_value()) {
                    statements.push_back(cast_any<Stmt>(result));
                }
            } else if (topCtx->externDecl()) {
                auto result = visit(topCtx->externDecl());
                if (result.has_value()) {
                    statements.push_back(cast_any<Stmt>(result));
                }
            } else if (topCtx->statement()) {
                auto result = visit(topCtx->statement());
                if (result.has_value()) {
                    statements.push_back(cast_any<Stmt>(result));
                }
            }
        }

        return (Node*)arena_.alloc<Program>(moduleName, std::move(useStmts), std::move(statements));
    }

    std::any visitStatement(LuvParser::StatementContext *ctx) override {
        if (ctx->funcDecl()) return (Node*)std::any_cast<Node*>(visit(ctx->funcDecl()));
        if (ctx->structDecl()) return (Node*)std::any_cast<Node*>(visit(ctx->structDecl()));
        if (ctx->enumDecl()) return (Node*)std::any_cast<Node*>(visit(ctx->enumDecl()));
        if (ctx->classDecl()) return (Node*)std::any_cast<Node*>(visit(ctx->classDecl()));
        if (ctx->interfaceDecl()) return (Node*)std::any_cast<Node*>(visit(ctx->interfaceDecl()));
        if (ctx->exprStmt()) return (Node*)std::any_cast<Node*>(visit(ctx->exprStmt()));
        if (ctx->returnStmt()) return (Node*)std::any_cast<Node*>(visit(ctx->returnStmt()));
        if (ctx->breakStmt()) return (Node*)std::any_cast<Node*>(visit(ctx->breakStmt()));
        if (ctx->continueStmt()) return (Node*)std::any_cast<Node*>(visit(ctx->continueStmt()));
        if (ctx->block()) return (Node*)std::any_cast<Node*>(visit(ctx->block()));
        if (ctx->varDecl()) return (Node*)std::any_cast<Node*>(visit(ctx->varDecl()));

        if (ctx->ifExpr()) {
            Expr* expr = cast_any<Expr>(visit(ctx->ifExpr()));
            return (Node*)arena_.alloc<ExprStmt>(expr);
        }
        if (ctx->whileExpr()) {
            Expr* expr = cast_any<Expr>(visit(ctx->whileExpr()));
            return (Node*)arena_.alloc<ExprStmt>(expr);
        }
        if (ctx->forExpr()) {
            Expr* expr = cast_any<Expr>(visit(ctx->forExpr()));
            return (Node*)arena_.alloc<ExprStmt>(expr);
        }
        if (ctx->loopExpr()) {
            Expr* expr = cast_any<Expr>(visit(ctx->loopExpr()));
            return (Node*)arena_.alloc<ExprStmt>(expr);
        }

        return std::any{};
    }

    std::any visitVisibilityDecl(LuvParser::VisibilityDeclContext *ctx) override {
        ASTVisibility vis = ASTVisibility::DEFAULT;
        if (ctx->getText().substr(0, 3) == "pub") {
            vis = ASTVisibility::PUBLIC;
        } else if (ctx->getText().substr(0, 4) == "priv") {
            vis = ASTVisibility::PRIVATE;
        }

        if (ctx->funcDecl()) {
            FuncDecl* func = cast_any<FuncDecl>(visit(ctx->funcDecl()));
            if (func) {
                func->visibility = vis;
                return (Node*)func;
            }
        }
        if (ctx->varDecl()) {
            VarDecl* var = cast_any<VarDecl>(visit(ctx->varDecl()));
            if (var) return (Node*)var;
        }
        if (ctx->classDecl()) {
            ClassDecl* cls = cast_any<ClassDecl>(visit(ctx->classDecl()));
            if (cls) {
                cls->visibility = vis;
                return (Node*)cls;
            }
        }
        if (ctx->structDecl()) {
            StructDecl* sd = cast_any<StructDecl>(visit(ctx->structDecl()));
            if (sd) {
                sd->visibility = vis;
                return (Node*)sd;
            }
        }
        if (ctx->enumDecl()) {
            EnumDecl* ed = cast_any<EnumDecl>(visit(ctx->enumDecl()));
            if (ed) {
                ed->visibility = vis;
                return (Node*)ed;
            }
        }
        return std::any{};
    }

    std::any visitExternDecl(LuvParser::ExternDeclContext *ctx) override {
        std::string abi = ctx->STRING() ? ctx->STRING()->getText() : "\"C\"";
        abi = abi.substr(1, abi.length() - 2);

        std::string name = ctx->IDENTIFIER()->getText();
        std::vector<Param> params;
        if (ctx->params()) {
            for (auto paramCtx : ctx->params()->param()) {
                std::vector<std::string> pAttrs = collectAttributes(paramCtx->modifier());
                params.push_back({
                    paramCtx->IDENTIFIER()->getText(),
                    paramCtx->type() ? paramCtx->type()->getText() : "",
                    paramCtx->getText().find("dyn") != std::string::npos,
                    paramCtx->getText().find("mut") != std::string::npos,
                    pAttrs
                });
            }
        }
        std::string returnType = ctx->type() ? ctx->type()->getText() : "";
        return (Node*)arena_.alloc<ExternDecl>(abi, name, std::move(params), returnType);
    }

    std::any visitStructDecl(LuvParser::StructDeclContext *ctx) override {
        std::vector<std::string> attrs = collectAttributes(ctx->attribute());
        std::string name = ctx->IDENTIFIER()->getText();
        std::vector<StructField> fields;
        std::vector<Stmt*> nestedDecls;
        for (auto memberCtx : ctx->structMember()) {
            if (memberCtx->structField()) {
                auto f = memberCtx->structField();
                fields.push_back({f->IDENTIFIER()->getText(), f->type()->getText()});
            } else if (memberCtx->declaration()) {
                auto decl = cast_any<Stmt>(visit(memberCtx->declaration()));
                if (decl) nestedDecls.push_back(decl);
            }
        }
        return (Node*)arena_.alloc<StructDecl>(name, std::move(fields), std::move(nestedDecls), std::move(attrs));
    }

    std::any visitEnumDecl(LuvParser::EnumDeclContext *ctx) override {
        std::vector<std::string> attrs = collectAttributes(ctx->attribute());
        std::string name = ctx->IDENTIFIER()->getText();
        std::vector<EnumVariant> variants;
        for (auto varCtx : ctx->enumVariant()) {
            std::vector<std::string> types;
            if (varCtx->typeList()) {
                for (auto t : varCtx->typeList()->type()) types.push_back(t->getText());
            }
            variants.push_back({varCtx->IDENTIFIER()->getText(), std::move(types)});
        }
        return (Node*)arena_.alloc<EnumDecl>(name, std::move(variants), std::move(attrs));
    }

    std::any visitClassDecl(LuvParser::ClassDeclContext *ctx) override {
        std::vector<std::string> attrs = collectAttributes(ctx->attribute());
        std::string name = ctx->IDENTIFIER(0)->getText();
        bool isAbstract = ctx->getText().find("abstract") != std::string::npos;
        std::vector<std::string> baseAndInterfaces;
        for (size_t i = 1; i < ctx->IDENTIFIER().size(); ++i) {
            baseAndInterfaces.push_back(ctx->IDENTIFIER(i)->getText());
        }
        std::vector<ClassField> fields;
        std::vector<FuncDecl*> methods;
        std::vector<Stmt*> nestedDecls;
        for (auto memberCtx : ctx->classMember()) {
            std::vector<std::string> mAttrs = collectAttributes(memberCtx->attribute());
            if (memberCtx->classField()) {
                auto f = memberCtx->classField();
                fields.push_back({
                    f->IDENTIFIER()->getText(),
                    f->type()->getText(),
                    memberCtx->getText().find("priv") != std::string::npos,
                    mAttrs
                });
            } else if (memberCtx->funcDecl()) {
                FuncDecl* method = cast_any<FuncDecl>(visit(memberCtx->funcDecl()));
                if (method) {
                    method->boundStruct = name;
                    method->isOverride = memberCtx->getText().find("override") != std::string::npos;
                    method->isStatic = memberCtx->getText().find("static") != std::string::npos;
                    method->attributes.insert(method->attributes.begin(), mAttrs.begin(), mAttrs.end());
                    methods.push_back(method);
                }
            } else if (memberCtx->declaration()) {
                auto decl = cast_any<Stmt>(visit(memberCtx->declaration()));
                if (decl) nestedDecls.push_back(decl);
            }
        }
        return (Node*)arena_.alloc<ClassDecl>(name, isAbstract, std::move(baseAndInterfaces), std::move(fields), std::move(methods), std::move(nestedDecls), std::move(attrs));
    }

    std::any visitDeclaration(LuvParser::DeclarationContext *ctx) override {
        if (ctx->structDecl()) return (Node*)std::any_cast<Node*>(visit(ctx->structDecl()));
        if (ctx->enumDecl()) return (Node*)std::any_cast<Node*>(visit(ctx->enumDecl()));
        if (ctx->classDecl()) return (Node*)std::any_cast<Node*>(visit(ctx->classDecl()));
        if (ctx->interfaceDecl()) return (Node*)std::any_cast<Node*>(visit(ctx->interfaceDecl()));
        if (ctx->funcDecl()) return (Node*)std::any_cast<Node*>(visit(ctx->funcDecl()));
        if (ctx->varDecl()) return (Node*)std::any_cast<Node*>(visit(ctx->varDecl()));
        return std::any{};
    }

    std::any visitInterfaceDecl(LuvParser::InterfaceDeclContext *ctx) override {
        std::vector<std::string> attrs = collectAttributes(ctx->attribute());
        std::string name = ctx->IDENTIFIER(0)->getText();
        std::vector<std::string> bases;
        for (size_t i = 1; i < ctx->IDENTIFIER().size(); ++i) {
            bases.push_back(ctx->IDENTIFIER(i)->getText());
        }
        std::vector<InterfaceMethod> methods;
        for (auto memberCtx : ctx->interfaceMember()) {
            std::string mname = memberCtx->IDENTIFIER()->getText();
            std::vector<Param> params;
            if (memberCtx->params()) {
                for (auto p : memberCtx->params()->param()) {
                    std::vector<std::string> pAttrs = collectAttributes(p->modifier());
                    Expr* defaultVal = p->expr() ? cast_any<Expr>(visit(p->expr())) : nullptr;
                    params.push_back({
                        p->IDENTIFIER()->getText(),
                        p->type() ? p->type()->getText() : "",
                        p->getText().find("dyn") != std::string::npos,
                        p->getText().find("mut") != std::string::npos,
                        pAttrs, defaultVal
                    });
                }
            }
            methods.push_back({
                mname, std::move(params),
                memberCtx->type() ? memberCtx->type()->getText() : ""
            });
        }
        return (Node*)arena_.alloc<InterfaceDecl>(name, std::move(methods), std::move(bases), std::move(attrs));
    }

    std::any visitVarDecl(LuvParser::VarDeclContext *ctx) override {
        bool isMut = false, isConst = false, isDyn = false;
        std::string rssState = "";
        std::vector<std::string> attrs;
        for (auto m : ctx->modifier()) {
            std::string text = m->getText();
            if (text == "mut") isMut = true;
            else if (text == "const") isConst = true;
            else if (text == "dyn") isDyn = true;
            else if (text == "unique" || text == "lent" || text == "rc_shared" || 
                     text == "owned_cow" || text == "immutable" || text == "escaped") {
                rssState = text;
            }
            else if (m->attribute()) attrs.push_back(m->attribute()->getText());
        }
        std::string type = ctx->type() ? ctx->type()->getText() : "";
        Expr* init = ctx->expr() ? cast_any<Expr>(visit(ctx->expr())) : (Expr*)arena_.alloc<NullExpr>();
        
        auto pats = ctx->bindingPatternList()->bindingPattern();
        Pattern* pat = nullptr;
        if (pats.size() == 1) {
            pat = cast_any<Pattern>(visit(pats[0]));
        } else {
            std::vector<Pattern*> elements;
            for (auto p : pats) elements.push_back(cast_any<Pattern>(visit(p)));
            pat = (Pattern*)arena_.alloc<TuplePattern>(std::move(elements));
        }
        return (Node*)arena_.alloc<VarDecl>(pat, type, isMut, isConst, isDyn, attrs, init, rssState);
    }

    std::any visitIdentifierPattern(LuvParser::IdentifierPatternContext *ctx) override {
        return (Node*)arena_.alloc<IdentifierPattern>(ctx->IDENTIFIER()->getText());
    }

    std::any visitTuplePattern(LuvParser::TuplePatternContext *ctx) override {
        std::vector<Pattern*> elements;
        for (auto bp : ctx->bindingPatternList()->bindingPattern()) {
            elements.push_back(cast_any<Pattern>(visit(bp)));
        }
        return (Node*)arena_.alloc<TuplePattern>(std::move(elements));
    }

    std::any visitStructPattern(LuvParser::StructPatternContext *ctx) override {
        std::vector<std::pair<std::string, Pattern*>> fields;
        if (ctx->structBindingList()) {
            for (auto sb : ctx->structBindingList()->structBinding()) {
                std::string name = sb->IDENTIFIER()->getText();
                Pattern* pat = sb->bindingPattern() ? cast_any<Pattern>(visit(sb->bindingPattern())) : (Pattern*)arena_.alloc<IdentifierPattern>(name);
                fields.push_back({name, pat});
            }
        }
        return (Node*)arena_.alloc<StructPattern>(ctx->IDENTIFIER()->getText(), std::move(fields));
    }

    std::any visitVariantPattern(LuvParser::VariantPatternContext *ctx) override {
        std::vector<Pattern*> elements;
        if (ctx->bindingPatternList()) {
            for (auto bp : ctx->bindingPatternList()->bindingPattern()) {
                elements.push_back(cast_any<Pattern>(visit(bp)));
            }
        }
        return (Node*)arena_.alloc<VariantPattern>(ctx->IDENTIFIER()->getText(), std::move(elements));
    }

    std::any visitWildcardPattern(LuvParser::WildcardPatternContext *ctx) override {
        return (Node*)arena_.alloc<WildcardPattern>();
    }

    std::any visitLiteralPattern(LuvParser::LiteralPatternContext *ctx) override {
        if (ctx->RANGE() || ctx->RANGE_INC()) {
            Expr* start = cast_any<Expr>(visit(ctx->literal(0)));
            Expr* end = cast_any<Expr>(visit(ctx->literal(1)));
            return (Node*)arena_.alloc<RangePattern>(start, end, ctx->RANGE_INC() != nullptr);
        }
        return (Node*)arena_.alloc<LiteralPattern>(cast_any<Expr>(visit(ctx->literal(0))));
    }

    std::any visitNullCoalescingExpr(LuvParser::NullCoalescingExprContext *ctx) override {
        return (Node*)arena_.alloc<NullCoalescingExpr>(cast_any<Expr>(visit(ctx->left)), cast_any<Expr>(visit(ctx->right)));
    }

    std::any visitFuncDecl(LuvParser::FuncDeclContext *ctx) override {
        std::vector<std::string> attrs = collectAttributes(ctx->attribute());
        std::string name = ctx->funcName()->getText();
        std::string boundStruct;
        
        if (ctx->boundStruct) {
            boundStruct = ctx->boundStruct->getText();
        }
        
        std::vector<std::string> typeParams;
        if (ctx->typeParams()) {
            for (auto t : ctx->typeParams()->IDENTIFIER()) typeParams.push_back(t->getText());
        }
        std::vector<Param> params;
        if (ctx->params()) {
            for (auto paramCtx : ctx->params()->param()) {
                Expr* dVal = paramCtx->expr() ? cast_any<Expr>(visit(paramCtx->expr())) : nullptr;
                std::vector<std::string> pAttrs = collectAttributes(paramCtx->modifier());
                params.push_back({
                    paramCtx->IDENTIFIER()->getText(),
                    paramCtx->type() ? paramCtx->type()->getText() : "",
                    paramCtx->getText().find("dyn") != std::string::npos,
                    paramCtx->getText().find("mut") != std::string::npos,
                    pAttrs, dVal
                });
            }
        }
        std::string returnType = ctx->type() ? ctx->type()->getText() : "";
        Block* body = nullptr;
        if (ctx->block()) {
            body = cast_any<Block>(visitBlock(ctx->block()));
        } else if (ctx->expr()) {
            Expr* result = cast_any<Expr>(visit(ctx->expr()));
            std::vector<Stmt*> stmts;
            stmts.push_back((Stmt*)arena_.alloc<ReturnStmt>(result));
            body = arena_.alloc<Block>(std::move(stmts));
        }
        return (Node*)arena_.alloc<FuncDecl>(boundStruct, name, std::move(typeParams), std::move(params), returnType, body, std::move(attrs));
    }

    std::any visitAssignmentExpr(LuvParser::AssignmentExprContext *ctx) override {
        std::vector<Expr*> targets;
        for (auto* sat : ctx->target->singleAssignmentTarget()) {
            targets.push_back(cast_any<Expr>(visitSingleAssignmentTarget(sat)));
        }
        Expr* value = cast_any<Expr>(visit(ctx->value));
        return (Node*)arena_.alloc<Assignment>(std::move(targets), value, ctx->op->getText());
    }

    std::any visitSingleAssignmentTarget(LuvParser::SingleAssignmentTargetContext *ctx) override {
        Expr* current = cast_any<Expr>(visit(ctx->primary()));
        for (size_t i = 1; i < ctx->children.size(); ++i) {
            auto* child = ctx->children[i];
            std::string text = child->getText();
            if (text == "." || text == "->") {
                std::string member = ctx->children[++i]->getText();
                current = (Expr*)arena_.alloc<PropertyExpr>(current, member);
            } else if (text == "[") {
                // Find the expr child
                Expr* index = nullptr;
                for (size_t j = i + 1; j < ctx->children.size(); ++j) {
                    if (auto* ec = dynamic_cast<LuvParser::ExprContext*>(ctx->children[j])) {
                        index = cast_any<Expr>(visit(ec));
                        i = j;
                        break;
                    }
                }
                current = (Expr*)arena_.alloc<IndexExpr>(current, index);
                i++; // skip ']'
            } else if (text == "(") {
                std::vector<Expr*> args;
                for (size_t j = i + 1; j < ctx->children.size(); ++j) {
                    if (auto* ec = dynamic_cast<LuvParser::ExprContext*>(ctx->children[j])) {
                        args.push_back(cast_any<Expr>(visit(ec)));
                    } else if (ctx->children[j]->getText() == ")") {
                        i = j;
                        break;
                    }
                }
                current = (Expr*)arena_.alloc<MethodCallExpr>(current, "", std::move(args));
            }
        }
        return (Node*)current;
    }

    std::any visitIndexExpr(LuvParser::IndexExprContext *ctx) override {
        return (Node*)arena_.alloc<IndexExpr>(cast_any<Expr>(visit(ctx->expr(0))), cast_any<Expr>(visit(ctx->expr(1))));
    }

    std::any visitPostfixExpr(LuvParser::PostfixExprContext *ctx) override {
        return (Node*)arena_.alloc<PostfixExpr>(cast_any<Expr>(visit(ctx->expr())), ctx->op->getText());
    }

    std::any visitIfExpr(LuvParser::IfExprContext *ctx) override {
        std::vector<std::string> attrs = collectAttributes(ctx->attribute());
        Expr* cond = cast_any<Expr>(visit(ctx->expr()));
        Block* thenBlock = cast_any<Block>(visitBlock(ctx->block(0)));
        std::vector<EfExpr> efs;
        for (auto efCtx : ctx->efExpr()) {
            efs.push_back({ cast_any<Expr>(visit(efCtx->expr())), cast_any<Block>(visitBlock(efCtx->block())) });
        }
        Block* elseBlock = nullptr;
        if (ctx->block().size() > (1 + ctx->efExpr().size())) {
            elseBlock = cast_any<Block>(visitBlock(ctx->block().back()));
        }
        return (Node*)arena_.alloc<IfExpr>(cond, thenBlock, std::move(efs), elseBlock, std::move(attrs));
    }

    std::any visitWhileExpr(LuvParser::WhileExprContext *ctx) override {
        std::vector<std::string> attrs = collectAttributes(ctx->attribute());
        if (ctx->IDENTIFIER()) attrs.push_back(ctx->IDENTIFIER()->getText());
        Expr* cond = cast_any<Expr>(visit(ctx->expr()));
        Block* thenBlock = cast_any<Block>(visitBlock(ctx->block(0)));
        Block* continueBlock = nullptr;
        if (ctx->block().size() > 1) {
            continueBlock = cast_any<Block>(visitBlock(ctx->block(1)));
        }
        return (Node*)arena_.alloc<WhileExpr>(cond, thenBlock, continueBlock, std::move(attrs));
    }

    std::any visitLoopExpr(LuvParser::LoopExprContext *ctx) override {
        std::vector<std::string> attrs = collectAttributes(ctx->attribute());
        if (ctx->IDENTIFIER()) attrs.push_back(ctx->IDENTIFIER()->getText());
        Block* body = cast_any<Block>(visitBlock(ctx->block()));
        return (Node*)arena_.alloc<LoopExpr>(body, std::move(attrs));
    }

    std::any visitForRepeatExpr(LuvParser::ForRepeatExprContext *ctx) override {
        std::vector<std::string> attrs = collectAttributes(ctx->attribute());
        if (ctx->IDENTIFIER()) attrs.push_back(ctx->IDENTIFIER()->getText());
        Expr* start = cast_any<Expr>(visit(ctx->start));
        Expr* end = cast_any<Expr>(visit(ctx->end));
        bool inclusive = ctx->RANGE_INC() != nullptr;
        Block* body = cast_any<Block>(visitBlock(ctx->block()));
        return (Node*)arena_.alloc<ForRepeatExpr>(start, end, inclusive, body, std::move(attrs));
    }

    std::any visitForRangeExpr(LuvParser::ForRangeExprContext *ctx) override {
        std::vector<std::string> attrs = collectAttributes(ctx->attribute());
        if (ctx->IDENTIFIER()) attrs.push_back(ctx->IDENTIFIER()->getText());
        bool isDynamic = false;
        for (auto* m : ctx->modifier()) {
            if (m->getText() == "dyn") isDynamic = true;
        }
        Expr* start = cast_any<Expr>(visit(ctx->start));
        Expr* end = cast_any<Expr>(visit(ctx->end));
        Block* body = cast_any<Block>(visitBlock(ctx->block()));
        
        auto pats = ctx->bindingPatternList()->bindingPattern();
        Pattern* pat = nullptr;
        if (pats.size() == 1) {
            pat = cast_any<Pattern>(visit(pats[0]));
        } else {
            std::vector<Pattern*> elements;
            for (auto p : pats) elements.push_back(cast_any<Pattern>(visit(p)));
            pat = (Pattern*)arena_.alloc<TuplePattern>(std::move(elements));
        }
        return (Node*)arena_.alloc<ForRangeExpr>(pat, isDynamic, start, end, false, body, std::move(attrs));
    }

    std::any visitForRangeIncExpr(LuvParser::ForRangeIncExprContext *ctx) override {
        std::vector<std::string> attrs = collectAttributes(ctx->attribute());
        if (ctx->IDENTIFIER()) attrs.push_back(ctx->IDENTIFIER()->getText());
        bool isDynamic = false;
        for (auto* m : ctx->modifier()) {
            if (m->getText() == "dyn") isDynamic = true;
        }
        Expr* start = cast_any<Expr>(visit(ctx->start));
        Expr* end = cast_any<Expr>(visit(ctx->end));
        Block* body = cast_any<Block>(visitBlock(ctx->block()));
        
        auto pats = ctx->bindingPatternList()->bindingPattern();
        Pattern* pat = nullptr;
        if (pats.size() == 1) {
            pat = cast_any<Pattern>(visit(pats[0]));
        } else {
            std::vector<Pattern*> elements;
            for (auto p : pats) elements.push_back(cast_any<Pattern>(visit(p)));
            pat = (Pattern*)arena_.alloc<TuplePattern>(std::move(elements));
        }
        return (Node*)arena_.alloc<ForRangeExpr>(pat, isDynamic, start, end, true, body, std::move(attrs));
    }

    std::any visitForInExpr(LuvParser::ForInExprContext *ctx) override {
        std::vector<std::string> attrs = collectAttributes(ctx->attribute());
        if (ctx->IDENTIFIER()) attrs.push_back(ctx->IDENTIFIER()->getText());
        bool isDynamic = false;
        for (auto* m : ctx->modifier()) {
            if (m->getText() == "dyn") isDynamic = true;
        }
        Expr* iterable = cast_any<Expr>(visit(ctx->expr()));
        Block* body = cast_any<Block>(visitBlock(ctx->block()));
        
        auto pats = ctx->bindingPatternList()->bindingPattern();
        Pattern* pat = nullptr;
        if (pats.size() == 1) {
            pat = cast_any<Pattern>(visit(pats[0]));
        } else {
            std::vector<Pattern*> elements;
            for (auto p : pats) elements.push_back(cast_any<Pattern>(visit(p)));
            pat = (Pattern*)arena_.alloc<TuplePattern>(std::move(elements));
        }
        return (Node*)arena_.alloc<ForInExpr>(pat, isDynamic, iterable, body, std::move(attrs));
    }

    std::any visitForCStyle(LuvParser::ForCStyleContext *ctx) override {
        std::vector<std::string> attrs = collectAttributes(ctx->attribute());
        if (ctx->IDENTIFIER()) attrs.push_back(ctx->IDENTIFIER()->getText());
        Stmt *init = nullptr, *step = nullptr;
        Expr* cond = nullptr;
        
        size_t exprIdx = 0;
        if (ctx->varDecl()) {
            init = cast_any<Stmt>(visit(ctx->varDecl()));
        } else if (!ctx->expr().empty()) {
            init = (Stmt*)arena_.alloc<ExprStmt>(cast_any<Expr>(visit(ctx->expr(exprIdx++))));
        }
        
        if (exprIdx < ctx->expr().size()) {
            cond = cast_any<Expr>(visit(ctx->expr(exprIdx++)));
        }
        if (exprIdx < ctx->expr().size()) {
            step = (Stmt*)arena_.alloc<ExprStmt>(cast_any<Expr>(visit(ctx->expr(exprIdx++))));
        }
        
        Block* body = cast_any<Block>(visitBlock(ctx->block()));
        return (Node*)arena_.alloc<ForCStyleExpr>(init, cond, step, body, std::move(attrs));
    }


    std::any visitReturnStmt(LuvParser::ReturnStmtContext *ctx) override {
        Expr* val = ctx->expr() ? cast_any<Expr>(visit(ctx->expr())) : (Expr*)arena_.alloc<NullExpr>();
        return (Node*)arena_.alloc<ReturnStmt>(val);
    }

    std::any visitBreakStmt(LuvParser::BreakStmtContext *ctx) override {
        return (Node*)arena_.alloc<BreakStmt>(ctx->IDENTIFIER() ? ctx->IDENTIFIER()->getText() : "");
    }

    std::any visitContinueStmt(LuvParser::ContinueStmtContext *ctx) override {
        return (Node*)arena_.alloc<ContinueStmt>(ctx->IDENTIFIER() ? ctx->IDENTIFIER()->getText() : "");
    }

    std::any visitExprStmt(LuvParser::ExprStmtContext *ctx) override {
        return (Node*)arena_.alloc<ExprStmt>(cast_any<Expr>(visit(ctx->expr())));
    }

    std::any visitBlock(LuvParser::BlockContext *ctx) override {
        std::vector<Stmt*> stmts;
        for (auto sCtx : ctx->statement()) {
            std::any res = visit(sCtx);
            if (res.has_value()) stmts.push_back(cast_any<Stmt>(res));
        }
        return (Node*)arena_.alloc<Block>(std::move(stmts));
    }

    std::any visitLogicalOrExpr(LuvParser::LogicalOrExprContext *ctx) override { return (Node*)arena_.alloc<BinaryExpr>(cast_any<Expr>(visit(ctx->left)), ctx->op->getText(), cast_any<Expr>(visit(ctx->right))); }
    std::any visitLogicalAndExpr(LuvParser::LogicalAndExprContext *ctx) override { return (Node*)arena_.alloc<BinaryExpr>(cast_any<Expr>(visit(ctx->left)), ctx->op->getText(), cast_any<Expr>(visit(ctx->right))); }
    std::any visitBitwiseOrExpr(LuvParser::BitwiseOrExprContext *ctx) override { return (Node*)arena_.alloc<BinaryExpr>(cast_any<Expr>(visit(ctx->left)), ctx->op->getText(), cast_any<Expr>(visit(ctx->right))); }
    std::any visitBitwiseXorExpr(LuvParser::BitwiseXorExprContext *ctx) override { return (Node*)arena_.alloc<BinaryExpr>(cast_any<Expr>(visit(ctx->left)), ctx->op->getText(), cast_any<Expr>(visit(ctx->right))); }
    std::any visitBitwiseAndExpr(LuvParser::BitwiseAndExprContext *ctx) override { return (Node*)arena_.alloc<BinaryExpr>(cast_any<Expr>(visit(ctx->left)), ctx->op->getText(), cast_any<Expr>(visit(ctx->right))); }
    std::any visitShiftExpr(LuvParser::ShiftExprContext *ctx) override { return (Node*)arena_.alloc<BinaryExpr>(cast_any<Expr>(visit(ctx->left)), ctx->op->getText(), cast_any<Expr>(visit(ctx->right))); }
    std::any visitAdditiveExpr(LuvParser::AdditiveExprContext *ctx) override {
        return (Node*)arena_.alloc<BinaryExpr>(cast_any<Expr>(visit(ctx->left)), ctx->op->getText(), cast_any<Expr>(visit(ctx->right)));
    }
    std::any visitMultiplicativeExpr(LuvParser::MultiplicativeExprContext *ctx) override {
        return (Node*)arena_.alloc<BinaryExpr>(cast_any<Expr>(visit(ctx->left)), ctx->op->getText(), cast_any<Expr>(visit(ctx->right)));
    }
    std::any visitUnaryExpr(LuvParser::UnaryExprContext *ctx) override { return (Node*)arena_.alloc<UnaryExpr>(ctx->op->getText(), cast_any<Expr>(visit(ctx->expr()))); }
    std::any visitTernaryExpr(LuvParser::TernaryExprContext *ctx) override { return (Node*)arena_.alloc<TernaryExpr>(cast_any<Expr>(visit(ctx->cond)), cast_any<Expr>(visit(ctx->thenExpr)), cast_any<Expr>(visit(ctx->elseExpr))); }
    std::any visitComparisonExpr(LuvParser::ComparisonExprContext *ctx) override {
        Expr *L = cast_any<Expr>(visit(ctx->left)), *R = cast_any<Expr>(visit(ctx->right));
        std::string op = ctx->op->getText(); if (op == "=") op = "==";
        if (auto* chain = dynamic_cast<ComparisonChainExpr*>(L)) { chain->operands.push_back(R); chain->operators.push_back(op); return (Node*)chain; }
        return (Node*)arena_.alloc<ComparisonChainExpr>(std::vector<Expr*>{L, R}, std::vector<std::string>{op});
    }

    std::any visitMatchExpr(LuvParser::MatchExprContext *ctx) override {
        Expr* val = cast_any<Expr>(visit(ctx->expr()));
        std::vector<MatchCase> cases;
        for (auto mctx : ctx->matchCase()) {
            MatchCase mc;
            mc.pattern = cast_any<Pattern>(visit(mctx->pattern));
            if (mctx->resultExpr) mc.resultExpr = cast_any<Expr>(visit(mctx->resultExpr));
            else if (mctx->resultBlock) mc.resultBlock = cast_any<Block>(visitBlock(mctx->resultBlock));
            cases.push_back(mc);
        }
        return (Node*)arena_.alloc<MatchExpr>(val, std::move(cases));
    }

    std::any visitCallExpr(LuvParser::CallExprContext *ctx) override {
        std::vector<Expr*> args;
        if (ctx->args()) for (auto argCtx : ctx->args()->expr()) args.push_back(cast_any<Expr>(visit(argCtx)));
        return (Node*)arena_.alloc<CallExpr>(ctx->IDENTIFIER()->getText(), std::move(args));
    }

    std::any visitGenericCallExpr(LuvParser::GenericCallExprContext *ctx) override {
        std::vector<std::string> tArgs; for (auto t : ctx->type()) tArgs.push_back(t->getText());
        std::vector<Expr*> args;
        if (ctx->args()) for (auto argCtx : ctx->args()->expr()) args.push_back(cast_any<Expr>(visit(argCtx)));
        return (Node*)arena_.alloc<GenericCallExpr>(ctx->IDENTIFIER()->getText(), std::move(tArgs), std::move(args));
    }

    std::any visitStructInstExpr(LuvParser::StructInstExprContext *ctx) override {
        std::vector<std::pair<std::string, Expr*>> fields;
        if (ctx->structInstFields()) {
            auto ids = ctx->structInstFields()->IDENTIFIER(); auto exprs = ctx->structInstFields()->expr();
            for (size_t i = 0; i < ids.size(); ++i) fields.push_back({ids[i]->getText(), cast_any<Expr>(visit(exprs[i]))});
        }
        return (Node*)arena_.alloc<StructInstExpr>(ctx->IDENTIFIER()->getText(), std::move(fields));
    }

    std::any visitIntrinsicCallExpr(LuvParser::IntrinsicCallExprContext *ctx) override {
        std::vector<Expr*> args;
        if (ctx->args()) for (auto argCtx : ctx->args()->expr()) args.push_back(cast_any<Expr>(visit(argCtx)));
        return (Node*)arena_.alloc<IntrinsicCallExpr>(ctx->IDENTIFIER()->getText(), std::move(args));
    }

    std::any visitAsmExpr(LuvParser::AsmExprContext *ctx) override {
        std::string s = ctx->STRING() ? ctx->STRING()->getText() : ctx->BACKTICK_STRING()->getText();
        std::vector<Expr*> args;
        if (ctx->args()) {
            for (auto* e : ctx->args()->expr()) args.push_back(cast_any<Expr>(visit(e)));
        }
        Expr* rtn = ctx->expr() ? cast_any<Expr>(visit(ctx->expr())) : nullptr;
        return (Node*)arena_.alloc<AsmExpr>(processEscapeSequences(s.substr(1, s.length() - 2)), std::move(args), rtn);
    }

    std::any visitCastExpr(LuvParser::CastExprContext *ctx) override {
        std::string op = ctx->op->getText();
        return (Node*)arena_.alloc<CastExpr>(cast_any<Expr>(visit(ctx->expr())), ctx->type()->getText(), op == "as!" || op == "|>");
    }

    std::any visitPropertyExpr(LuvParser::PropertyExprContext *ctx) override {
        std::string name;
        if (ctx->IDENTIFIER()) name = ctx->IDENTIFIER()->getText();
        else if (ctx->INT()) name = ctx->INT()->getText();
        else if (ctx->FLOAT()) name = ctx->FLOAT()->getText();
        return (Node*)arena_.alloc<PropertyExpr>(cast_any<Expr>(visit(ctx->expr())), name);
    }

    std::any visitMethodCallExpr(LuvParser::MethodCallExprContext *ctx) override {
        std::vector<Expr*> args;
        if (ctx->args()) for (auto argCtx : ctx->args()->expr()) args.push_back(cast_any<Expr>(visit(argCtx)));
        std::string name;
        if (ctx->IDENTIFIER()) name = ctx->IDENTIFIER()->getText();
        else if (ctx->INT()) name = ctx->INT()->getText();
        else if (ctx->FLOAT()) name = ctx->FLOAT()->getText();
        return (Node*)arena_.alloc<MethodCallExpr>(cast_any<Expr>(visit(ctx->expr())), name, std::move(args));
    }

    std::any visitSuperCallExpr(LuvParser::SuperCallExprContext *ctx) override {
        std::vector<Expr*> args;
        if (ctx->args()) for (auto argCtx : ctx->args()->expr()) args.push_back(cast_any<Expr>(visit(argCtx)));
        return (Node*)arena_.alloc<SuperCallExpr>(ctx->IDENTIFIER()->getText(), std::move(args));
    }

    std::any visitPrimaryLiteral(LuvParser::PrimaryLiteralContext *ctx) override { return (Node*)std::any_cast<Node*>(visit(ctx->literal())); }
    std::any visitIntLit(LuvParser::IntLitContext *ctx) override { return (Node*)arena_.alloc<IntExpr>(ctx->INT()->getText()); }
    std::any visitFloatLit(LuvParser::FloatLitContext *ctx) override { return (Node*)arena_.alloc<FloatExpr>(ctx->FLOAT()->getText()); }
    std::any visitStringLit(LuvParser::StringLitContext *ctx) override {
        std::string s = ctx->STRING() ? ctx->STRING()->getText() : ctx->BACKTICK_STRING()->getText();
        return (Node*)arena_.alloc<StringExpr>(processEscapeSequences(s.substr(1, s.length() - 2)));
    }
    std::any visitCharLit(LuvParser::CharLitContext *ctx) override { return (Node*)arena_.alloc<CharExpr>(ctx->CHAR()->getText()[1]); }
    std::any visitBoolLit(LuvParser::BoolLitContext *ctx) override { return (Node*)arena_.alloc<BoolExpr>(ctx->BOOL()->getText() == "true"); }
    std::any visitNullLit(LuvParser::NullLitContext *ctx) override { return (Node*)arena_.alloc<NullExpr>(); }
    std::any visitBytesLit(LuvParser::BytesLitContext *ctx) override {
        std::string s = ctx->BYTES_LIT()->getText();
        Expr* res = (Expr*)arena_.alloc<StringExpr>(processEscapeSequences(s.substr(2, s.length() - 3)));
        res->semanticType = "bytes";
        return (Node*)res;
    }

    std::any visitStringInterpolationExpr(LuvParser::StringInterpolationExprContext *ctx) override {
        std::string s = ctx->STRING() ? ctx->STRING()->getText() : ctx->BACKTICK_STRING()->getText();
        s = s.substr(1, s.length() - 2);
        
        std::vector<Expr*> parts;
        std::string currentLiteral;
        size_t i = 0;
        while (i < s.length()) {
            if (s[i] == '\\' && i + 1 < s.length()) {
                if (s[i+1] == '{') {
                    currentLiteral += '{';
                    i += 2;
                } else if (s[i+1] == '}') {
                    currentLiteral += '}';
                    i += 2;
                } else {
                    // Standard escape processing
                    std::string esc = s.substr(i, 2);
                    currentLiteral += processEscapeSequences(esc);
                    i += 2;
                }
            } else if (s[i] == '{') {
                if (!currentLiteral.empty()) {
                    parts.push_back((Expr*)arena_.alloc<StringExpr>(currentLiteral));
                    currentLiteral.clear();
                }
                i++;
                size_t start = i;
                int nest = 1;
                while (i < s.length() && nest > 0) {
                    if (s[i] == '{') nest++;
                    else if (s[i] == '}') nest--;
                    if (nest > 0) i++;
                }
                std::string eStr = s.substr(start, i - start);
                i++; // skip '}'
                
                antlr4::ANTLRInputStream in(eStr);
                LuvLexer lex(&in);
                antlr4::CommonTokenStream tks(&lex);
                LuvParser ps(&tks);
                if (auto* ec = ps.expr()) {
                    parts.push_back(cast_any<Expr>(visit(ec)));
                }
            } else {
                currentLiteral += s[i];
                i++;
            }
        }
        if (!currentLiteral.empty()) {
            parts.push_back((Expr*)arena_.alloc<StringExpr>(currentLiteral));
        }
        return (Node*)arena_.alloc<StringInterpolationExpr>(std::move(parts));
    }

    std::any visitPrimaryExpr(LuvParser::PrimaryExprContext *ctx) override {
        return (Node*)std::any_cast<Node*>(visit(ctx->primary()));
    }
    std::any visitIfExprAlternative(LuvParser::IfExprAlternativeContext *ctx) override { return (Node*)std::any_cast<Node*>(visit(ctx->ifExpr())); }
    std::any visitForExprAlternative(LuvParser::ForExprAlternativeContext *ctx) override { return (Node*)std::any_cast<Node*>(visit(ctx->forExpr())); }
    std::any visitWhileExprAlternative(LuvParser::WhileExprAlternativeContext *ctx) override { return (Node*)std::any_cast<Node*>(visit(ctx->whileExpr())); }
    std::any visitLoopExprAlternative(LuvParser::LoopExprAlternativeContext *ctx) override { return (Node*)std::any_cast<Node*>(visit(ctx->loopExpr())); }

    std::any visitIdentifier(LuvParser::IdentifierContext *ctx) override {
        return (Node*)arena_.alloc<VarExpr>(ctx->IDENTIFIER()->getText());
    }
    std::any visitGroupingExpr(LuvParser::GroupingExprContext *ctx) override {
        auto res = visit(ctx->expr());
        if (!res.has_value()) return std::any{};
        return (Node*)std::any_cast<Node*>(res);
    }
    std::any visitTupleExpr(LuvParser::TupleExprContext *ctx) override {
        std::vector<Expr*> els; for (auto e : ctx->expr()) els.push_back(cast_any<Expr>(visit(e)));
        return (Node*)arena_.alloc<TupleExpr>(std::move(els));
    }
    std::any visitArrayExpr(LuvParser::ArrayExprContext *ctx) override {
        std::vector<Expr*> els; if (ctx->args()) for (auto e : ctx->args()->expr()) els.push_back(cast_any<Expr>(visit(e)));
        return (Node*)arena_.alloc<ArrayExpr>(std::move(els));
    }
    std::any visitArrayRepeatExpr(LuvParser::ArrayRepeatExprContext *ctx) override { return (Node*)arena_.alloc<ArrayRepeatExpr>(cast_any<Expr>(visit(ctx->expr(0))), cast_any<Expr>(visit(ctx->expr(1)))); }
    std::any visitArrayCompExpr(LuvParser::ArrayCompExprContext *ctx) override {
        Expr *e = cast_any<Expr>(visit(ctx->expr(0))), *iter = cast_any<Expr>(visit(ctx->expr(1))), *re = nullptr, *st = nullptr; bool inc = false;
        if (ctx->expr().size() > 2) { re = cast_any<Expr>(visit(ctx->expr(2))); inc = ctx->RANGE_INC() != nullptr; if (ctx->expr().size() > 3) st = cast_any<Expr>(visit(ctx->expr(3))); }
        return (Node*)arena_.alloc<ArrayCompExpr>(e, ctx->IDENTIFIER()->getText(), iter, re, inc, st);
    }
    std::any visitSliceExpr(LuvParser::SliceExprContext *ctx) override {
        Expr *t = cast_any<Expr>(visit(ctx->expr(0))), *s = cast_any<Expr>(visit(ctx->expr(1))), *e = cast_any<Expr>(visit(ctx->expr(2))), *st = nullptr; bool inc = ctx->RANGE_INC() != nullptr;
        if (ctx->expr().size() > 3) st = cast_any<Expr>(visit(ctx->expr(3)));
        return (Node*)arena_.alloc<SliceExpr>(t, s, e, inc, st);
    }

private:
    std::vector<std::string> collectAttributes(const std::vector<LuvParser::AttributeContext*>& attrCtxs) {
        std::vector<std::string> attrs;
        for (auto* ctx : attrCtxs) {
            if (ctx->AT()) attrs.push_back(ctx->getText());
            else if (ctx->attrList()) for (auto* a : ctx->attrList()->attr()) attrs.push_back(a->getText());
        }
        return attrs;
    }

    std::vector<std::string> collectAttributes(const std::vector<LuvParser::ModifierContext*>& modCtxs) {
        std::vector<std::string> attrs;
        for (auto* ctx : modCtxs) {
            if (ctx->attribute()) {
                if (ctx->attribute()->AT()) attrs.push_back(ctx->attribute()->getText());
                else if (ctx->attribute()->attrList()) {
                    for (auto* a : ctx->attribute()->attrList()->attr()) attrs.push_back(a->getText());
                }
            }
        }
        return attrs;
    }

    std::string processEscapeSequences(const std::string& s) {
        std::string res;
        for (size_t i = 0; i < s.length(); ++i) {
            if (s[i] == '\\' && i + 1 < s.length()) {
                switch (s[i+1]) {
                    case 'n': res += '\n'; i++; break;
                    case 't': res += '\t'; i++; break;
                    case 'b': res += '\b'; i++; break;
                    case 'r': res += '\r'; i++; break;
                    case 'a': res += '\a'; i++; break;
                    case 'f': res += '\f'; i++; break;
                    case 'v': res += '\v'; i++; break;
                    case '\\': res += '\\'; i++; break;
                    case '\"': res += '\"'; i++; break;
                    case '\'': res += '\''; i++; break;
                    case '?': res += '\?'; i++; break;
                    default: res += s[i]; break;
                }
            } else {
                res += s[i];
            }
        }
        return res;
    }
    std::string currentFile_;
    Arena& arena_;
};

} // namespace luv
