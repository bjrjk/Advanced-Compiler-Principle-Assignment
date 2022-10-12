#pragma once
//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool --------------===//
//===----------------------------------------------------------------------===//
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cctype>
#include <vector>
#include <algorithm>
#include <unistd.h>

using namespace std;

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;

#include "InterpreterVisitor.h"

typedef unsigned int uint_t;

class StackFrame {
private:
    // StackFrame maps Variable Declaration to Value
    // Which are either integer or addresses (also represented using an Integer value)
    std::map<Decl *, int> mVars;
    std::map<Stmt *, int> mExprs;
    // The current stmt
    Stmt *mPC;
    int mRetVal;
public:
    StackFrame() : mVars(), mExprs(), mPC(), mRetVal(0) {}

    void bindDecl(Decl *decl, int val) {
        mVars[decl] = val;
    }

    int getDeclVal(Decl *decl) {
        assert(mVars.find(decl) != mVars.end());
        return mVars.find(decl)->second;
    }

    void bindStmt(Stmt *stmt, int val) {
        mExprs[stmt] = val;
    }

    int getStmtVal(Stmt *stmt) {
        assert(mExprs.find(stmt) != mExprs.end());
        return mExprs[stmt];
    }

    void setPC(Stmt *stmt) {
        mPC = stmt;
    }

    Stmt *getPC() {
        return mPC;
    }

    void setRetVal(int retVal) {
        mRetVal = retVal;
    }

    int getRetVal() const {
        return mRetVal;
    }
};

/// Heap maps address to a value

class Heap {
private:
    struct chunkMeta {
    private:
        uint_t begin; // in bytes
        uint_t length, capacity; // in bytes
        char *pointer; // real pointer in host
    public:
        chunkMeta(uint_t length, uint_t &addressAccumulator) : length(length) {
            this->begin = addressAccumulator;
            this->capacity = (length + 7) & 0xfffffff8; // align chunk to 8 bytes
            addressAccumulator += capacity;
            this->pointer = static_cast<char *>(malloc(this->capacity));
        }

        ~chunkMeta() {
            free(this->pointer);
            this->pointer = nullptr;
        }

        uint_t getBegin() const { return begin; }

        uint_t getLength() const { return length; }

        int get(uint_t byteOffset) const {
            assert(byteOffset < length && byteOffset < capacity);
            int *intElemPtr = reinterpret_cast<int *>(pointer + byteOffset);
            return *intElemPtr;
        }

        void set(uint_t byteOffset, int value) {
            assert(byteOffset < length && byteOffset < capacity);
            int *intElemPtr = reinterpret_cast<int *>(pointer + byteOffset);
            *intElemPtr = value;
        }
    };

    unsigned int addressAccumulator;
    vector<chunkMeta *> chunks;

    chunkMeta* queryChunkMeta(int addr) const {
        auto findIter = find_if(chunks.begin(), chunks.end(), [&](chunkMeta *chunk) {
            uint_t begin = chunk->getBegin(), length = chunk->getLength();
            uint_t end = begin + length;
            return begin <= addr && addr < end;
        });
        assert(findIter != chunks.end());
        if (findIter != chunks.end()) {
            return *findIter;
        } else {
            return nullptr;
        }
    }
public:
    Heap() : addressAccumulator(0) {}
    ~Heap() {
        for_each(chunks.begin(), chunks.end(), [](chunkMeta *&chunk) {
            delete chunk;
            chunk = nullptr;
        });
        chunks.clear();
    }

    int allocate(int size) {
        chunkMeta *chunk = new chunkMeta(size, addressAccumulator);
        chunks.push_back(chunk);
        return chunk->getBegin();
    }

    void release(int addr) {
        // Remove only when `addr` is chunk's begin address
        remove_if(chunks.begin(), chunks.end(), [&](chunkMeta *&chunk) {
            if (chunk->getBegin() == addr) {
                delete chunk;
                chunk = nullptr;
                return true;
            }
            return false;
        });
    }

    void set(int addr, int val) {
        chunkMeta *chunk = queryChunkMeta(addr);
        uint_t byteOffset = addr - chunk->getBegin();
        assert(byteOffset < chunk->getLength());
        chunk->set(byteOffset, val);
    }

    int get(int addr) const {
        chunkMeta *chunk = queryChunkMeta(addr);
        uint_t byteOffset = addr - chunk->getBegin();
        assert(byteOffset < chunk->getLength());
        return chunk->get(byteOffset);
    }
};


class Environment {
private:
    InterpreterVisitor *iVisitor;

    vector<StackFrame> dStack;
    Heap dHeap;

    FunctionDecl *fFree;        // Declarations to the built-in functions
    FunctionDecl *fMalloc;
    FunctionDecl *fInput;
    FunctionDecl *fOutput;
    FunctionDecl *fEntry;       // Program entrypoint

public:
    Environment() :fFree(nullptr), fMalloc(nullptr),
    fInput(nullptr), fOutput(nullptr), fEntry(nullptr) {}


    // Initialize the Environment
    void init(TranslationUnitDecl *unit, InterpreterVisitor *visitor) {
        iVisitor = visitor;
        for (TranslationUnitDecl::decl_iterator i = unit->decls_begin(), e = unit->decls_end(); i != e; ++i) {
            if (FunctionDecl *fdecl = dyn_cast<FunctionDecl>(*i)) {
                // Get the Declarations to the built-in functions
                if (fdecl->getName().equals("FREE")) fFree = fdecl;
                else if (fdecl->getName().equals("MALLOC")) fMalloc = fdecl;
                else if (fdecl->getName().equals("GET")) fInput = fdecl;
                else if (fdecl->getName().equals("PRINT")) fOutput = fdecl;
                else if (fdecl->getName().equals("main")) fEntry = fdecl;
            }
        }
        dStack.push_back(StackFrame());
    }

    FunctionDecl *getEntry() {
        return fEntry;
    }

    void integerLiteral(IntegerLiteral *intLiteral) {
        int literalVal = intLiteral->getValue().getSExtValue();
        dStack.back().bindStmt(intLiteral, literalVal);
    }

    void binop(BinaryOperator *bop) {
        Expr *LHSExpr = bop->getLHS();
        Expr *RHSExpr = bop->getRHS();
        auto opStr = bop->getOpcodeStr();

        if (opStr.equals("=")) { // Assignment
            int RHSVal = dStack.back().getStmtVal(RHSExpr);
            dStack.back().bindStmt(LHSExpr, RHSVal);
            if (DeclRefExpr *declRefLHSExpr = dyn_cast<DeclRefExpr>(LHSExpr)) {
                Decl *decl = declRefLHSExpr->getFoundDecl();
                dStack.back().bindDecl(decl, RHSVal);
            }
        } else { // Arithmatic, Comparative
            int LHSVal = dStack.back().getStmtVal(LHSExpr);
            int RHSVal = dStack.back().getStmtVal(RHSExpr);
            int result;
            if (opStr.equals("+")) {
                result = LHSVal + RHSVal;
            } else if (opStr.equals("-")) {
                result = LHSVal - RHSVal;
            } else if (opStr.equals("*")) {
                result = LHSVal * RHSVal;
            } else if (opStr.equals("/")) {
                result = LHSVal / RHSVal;
            } else if (opStr.equals("<")) {
                result = LHSVal < RHSVal;
            } else if (opStr.equals("<=")) {
                result = LHSVal <= RHSVal;
            } else if (opStr.equals(">")) {
                result = LHSVal > RHSVal;
            } else if (opStr.equals(">=")) {
                result = LHSVal >= RHSVal;
            } else if (opStr.equals("==")) {
                result = LHSVal == RHSVal;
            } else {
                assert(false);
                result = 0;
            }
            dStack.back().bindStmt(bop, result);
        }
    }

    void decl(DeclStmt *declstmt) {
        for (DeclStmt::decl_iterator it = declstmt->decl_begin(), ie = declstmt->decl_end();
             it != ie; ++it) {
            Decl *decl = *it;
            if (VarDecl *vardecl = dyn_cast<VarDecl>(decl)) {
                dStack.back().bindDecl(vardecl, 0);
            }
        }
    }

    void declref(DeclRefExpr *declref) { // TODO: Variable only on stack now
        dStack.back().setPC(declref);
        if (declref->getType()->isIntegerType()) {
            Decl *decl = declref->getFoundDecl();

            int val = dStack.back().getDeclVal(decl);
            dStack.back().bindStmt(declref, val);
        }
    }

    void cast(CastExpr *castexpr) {
        dStack.back().setPC(castexpr);
        if (castexpr->getType()->isIntegerType()) {
            Expr *expr = castexpr->getSubExpr();
            int val = dStack.back().getStmtVal(expr);
            dStack.back().bindStmt(castexpr, val);
        }
    }

    /// !TODO Support Function Call
    void call(CallExpr *callexpr) {
        dStack.back().setPC(callexpr);
        FunctionDecl *callee = callexpr->getDirectCallee();
        if (callee == fInput) {
            int val;
#ifndef ASSIGNMENT_DEBUG
            llvm::errs() << "Please Input an Integer Value : ";
#endif
            scanf("%d", &val);
            dStack.back().bindStmt(callexpr, val);
        } else if (callee == fOutput) {
            Expr *outputExp = callexpr->getArg(0);
            int val = dStack.back().getStmtVal(outputExp);
#ifndef ASSIGNMENT_DEBUG
            llvm::errs() << val;
#else
            printf("%d\n", val);
#endif
        } else if (callee == fMalloc) {
            Expr *chunkSizeExpr = callexpr->getArg(0);
            int chunkSize = dStack.back().getStmtVal(chunkSizeExpr);
            int chunkVMAddr = dHeap.allocate(chunkSize);
            dStack.back().bindStmt(callexpr, chunkVMAddr);
        } else if (callee == fFree) {
            Expr *chunkVMAddrExpr = callexpr->getArg(0);
            int chunkVMAddr = dStack.back().getStmtVal(chunkVMAddrExpr);
            dHeap.release(chunkVMAddr);
        } else { // For customized functions, handle call & return here
            // Create new call stack
            dStack.push_back(StackFrame());
            StackFrame &oldFrame = *(dStack.end() - 2), &newFrame = *(dStack.end() - 1);
            // Copy argument values to corresponding parameter bindings
            uint_t paramCount = callee->getNumParams();
            for (int i = 0; i < paramCount; i++) {
                Expr *argExpr = callexpr->getArg(i);
                int argVal = oldFrame.getStmtVal(argExpr);
                Decl *paramDecl = callee->getParamDecl(i);
                newFrame.bindDecl(paramDecl, argVal);
            }
            // Visit new function
            iVisitor->VisitStmt(callee->getBody());
            // Collect return value
            int retVal = newFrame.getRetVal();
            oldFrame.bindStmt(callexpr, retVal);
            // Pop call stack
            dStack.pop_back();
        }
    }

    void expr(Expr *expr) {

    }

    void ret(ReturnStmt *retStmt) {
         Expr *retExpr = retStmt->getRetValue();
         int retVal = dStack.back().getStmtVal(retExpr);
         dStack.back().setRetVal(retVal);
    }

    void ifStmt(IfStmt *ifStmt) {
        Expr *condExpr = ifStmt->getCond();
        iVisitor->Visit(condExpr);
        int condVal = dStack.back().getStmtVal(condExpr);
        if (condVal != 0) {
            iVisitor->Visit(ifStmt->getThen());
        } else {
            if (ifStmt->hasElseStorage()) {
                iVisitor->Visit(ifStmt->getElse());
            }
        }
    }

    void whileStmt(WhileStmt *whileStmt) {
        Expr *condExpr = whileStmt->getCond();
        while (true) {
            iVisitor->Visit(condExpr);
            int condVal = dStack.back().getStmtVal(condExpr);
            if (condVal == 0) break;
            iVisitor->Visit(whileStmt->getBody());
        }
    }

    void forStmt(ForStmt *forStmt) {
        iVisitor->Visit(forStmt->getInit());
        Expr *condExpr = forStmt->getCond();
        while (true) {
            iVisitor->Visit(condExpr);
            int condVal = dStack.back().getStmtVal(condExpr);
            if (condVal == 0) break;
            iVisitor->Visit(forStmt->getBody());
            iVisitor->Visit(forStmt->getInc());
        }
    }
};
