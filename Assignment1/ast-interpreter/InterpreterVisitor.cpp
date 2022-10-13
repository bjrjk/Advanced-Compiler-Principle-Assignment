#include "InterpreterVisitor.h"
#include "Environment.h"

void InterpreterVisitor::VisitIntegerLiteral(IntegerLiteral *intLiteral) {
    mEnv->integerLiteral(intLiteral);
}

void InterpreterVisitor::VisitBinaryOperator(BinaryOperator *bop) {
    VisitStmt(bop);
    mEnv->binop(bop);
}

void InterpreterVisitor::VisitUnaryOperator(UnaryOperator *uop) {
    VisitStmt(uop);
    mEnv->uop(uop);
}

void InterpreterVisitor::VisitDeclRefExpr(DeclRefExpr *expr) {
    VisitStmt(expr);
    mEnv->declref(expr);
}

void InterpreterVisitor::VisitCastExpr(CastExpr *expr) {
    VisitStmt(expr);
    mEnv->cast(expr);
}

void InterpreterVisitor::VisitCallExpr(CallExpr *call) {
    VisitStmt(call);
    mEnv->call(call);
}

void InterpreterVisitor::VisitDeclStmt(DeclStmt *declstmt) {
    mEnv->decl(declstmt);
}

void InterpreterVisitor::VisitExpr(Expr *expr) {
    VisitStmt(expr);
    mEnv->expr(expr);
}

void InterpreterVisitor::VisitReturnStmt(ReturnStmt *retStmt) {
    VisitStmt(retStmt);
    mEnv->ret(retStmt);
}

void InterpreterVisitor::VisitIfStmt(IfStmt *ifStmt) {
    mEnv->ifStmt(ifStmt);
}

void InterpreterVisitor::VisitWhileStmt(WhileStmt *whileStmt) {
    mEnv->whileStmt(whileStmt);
}

void InterpreterVisitor::VisitForStmt(ForStmt *forStmt) {
    mEnv->forStmt(forStmt);
}

void InterpreterVisitor::VisitStmt(Stmt *stmt) {
    mEnv->stmt(stmt);
}