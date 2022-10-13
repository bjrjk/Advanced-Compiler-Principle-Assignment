#include "InterpreterVisitor.h"
#include "Environment.h"

void InterpreterVisitor::VisitIntegerLiteral(IntegerLiteral *intLiteral) {
    mEnv->integerLiteral(intLiteral);
}

void InterpreterVisitor::VisitBinaryOperator(BinaryOperator *bop) {
    VisitStmt(bop);
    mEnv->binaryOperator(bop);
}

void InterpreterVisitor::VisitUnaryOperator(UnaryOperator *uop) {
    VisitStmt(uop);
    mEnv->unaryOperator(uop);
}

void InterpreterVisitor::VisitArraySubscriptExpr(ArraySubscriptExpr *arrSubExpr) {

}

void InterpreterVisitor::VisitDeclRefExpr(DeclRefExpr *expr) {
    VisitStmt(expr);
    mEnv->declRefExpr(expr);
}

void InterpreterVisitor::VisitCastExpr(CastExpr *expr) {
    VisitStmt(expr);
    mEnv->castExpr(expr);
}

void InterpreterVisitor::VisitCallExpr(CallExpr *call) {
    VisitStmt(call);
    mEnv->callExpr(call);
}

void InterpreterVisitor::VisitDeclStmt(DeclStmt *declstmt) {
    mEnv->declStmt(declstmt);
}

void InterpreterVisitor::VisitExpr(Expr *expr) {
    VisitStmt(expr);
    mEnv->expr(expr);
}

void InterpreterVisitor::VisitReturnStmt(ReturnStmt *retStmt) {
    VisitStmt(retStmt);
    mEnv->returnStmt(retStmt);
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