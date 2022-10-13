#pragma once

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/EvaluatedExprVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;

class Environment;

class InterpreterVisitor :
        public EvaluatedExprVisitor<InterpreterVisitor> {
public:
    explicit InterpreterVisitor(const ASTContext &context, Environment *env)
            : EvaluatedExprVisitor(context), mEnv(env) {}

    virtual ~InterpreterVisitor() {}

    virtual void VisitIntegerLiteral(IntegerLiteral *intLiteral);

    virtual void VisitBinaryOperator(BinaryOperator *bop);

    virtual void VisitUnaryOperator(UnaryOperator *uop);

    virtual void VisitDeclRefExpr(DeclRefExpr *expr);

    virtual void VisitCastExpr(CastExpr *expr);

    virtual void VisitCallExpr(CallExpr *call);

    virtual void VisitDeclStmt(DeclStmt *declstmt);

    virtual void VisitExpr(Expr *expr);

    virtual void VisitReturnStmt(ReturnStmt *retStmt);

    virtual void VisitIfStmt(IfStmt *ifStmt);

    virtual void VisitWhileStmt(WhileStmt *whileStmt);

    virtual void VisitForStmt(ForStmt *forStmt);

    virtual void VisitStmt(Stmt *stmt);

private:
    Environment *mEnv;
};