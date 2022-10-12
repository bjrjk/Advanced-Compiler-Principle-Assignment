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

typedef unsigned int uint_t;

class StackFrame {
    /// StackFrame maps Variable Declaration to Value
    /// Which are either integer or addresses (also represented using an Integer value)
    std::map<Decl *, int> mVars;
    std::map<Stmt *, int> mExprs;
    /// The current stmt
    Stmt *mPC;
public:
    StackFrame() : mVars(), mExprs(), mPC() {
    }

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
    std::vector<StackFrame> mStack;
    Heap heap;

    FunctionDecl *mFree;                /// Declartions to the built-in functions
    FunctionDecl *mMalloc;
    FunctionDecl *mInput;
    FunctionDecl *mOutput;

    FunctionDecl *mEntry;
public:
    /// Get the declartions to the built-in functions
    Environment() : mStack(), mFree(NULL), mMalloc(NULL), mInput(NULL), mOutput(NULL), mEntry(NULL) {
    }


    /// Initialize the Environment
    void init(TranslationUnitDecl *unit) {
        for (TranslationUnitDecl::decl_iterator i = unit->decls_begin(), e = unit->decls_end(); i != e; ++i) {
            if (FunctionDecl *fdecl = dyn_cast<FunctionDecl>(*i)) {
                if (fdecl->getName().equals("FREE")) mFree = fdecl;
                else if (fdecl->getName().equals("MALLOC")) mMalloc = fdecl;
                else if (fdecl->getName().equals("GET")) mInput = fdecl;
                else if (fdecl->getName().equals("PRINT")) mOutput = fdecl;
                else if (fdecl->getName().equals("main")) mEntry = fdecl;
            }
        }
        mStack.push_back(StackFrame());
    }

    FunctionDecl *getEntry() {
        return mEntry;
    }

    void integerLiteral(IntegerLiteral *intLiteral) {
        int literalVal = intLiteral->getValue().getSExtValue();
        mStack.back().bindStmt(intLiteral, literalVal);
    }

    void binop(BinaryOperator *bop) {
        Expr *LHSExpr = bop->getLHS();
        Expr *RHSExpr = bop->getRHS();
        auto opStr = bop->getOpcodeStr();

        if (opStr.equals("=")) { // Assignment
            int RHSVal = mStack.back().getStmtVal(RHSExpr);
            mStack.back().bindStmt(LHSExpr, RHSVal);
            if (DeclRefExpr *declRefLHSExpr = dyn_cast<DeclRefExpr>(LHSExpr)) {
                Decl *decl = declRefLHSExpr->getFoundDecl();
                mStack.back().bindDecl(decl, RHSVal);
            }
        } else { // Arithmatic, Comparative
            int LHSVal = mStack.back().getStmtVal(LHSExpr);
            int RHSVal = mStack.back().getStmtVal(RHSExpr);
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
            mStack.back().bindStmt(bop, result);
        }
    }

    void decl(DeclStmt *declstmt) {
        for (DeclStmt::decl_iterator it = declstmt->decl_begin(), ie = declstmt->decl_end();
             it != ie; ++it) {
            Decl *decl = *it;
            if (VarDecl *vardecl = dyn_cast<VarDecl>(decl)) {
                mStack.back().bindDecl(vardecl, 0);
            }
        }
    }

    void declref(DeclRefExpr *declref) { // TODO: Variable only on stack now
        mStack.back().setPC(declref);
        if (declref->getType()->isIntegerType()) {
            Decl *decl = declref->getFoundDecl();

            int val = mStack.back().getDeclVal(decl);
            mStack.back().bindStmt(declref, val);
        }
    }

    void cast(CastExpr *castexpr) {
        mStack.back().setPC(castexpr);
        if (castexpr->getType()->isIntegerType()) {
            Expr *expr = castexpr->getSubExpr();
            int val = mStack.back().getStmtVal(expr);
            mStack.back().bindStmt(castexpr, val);
        }
    }

    /// !TODO Support Function Call
    void call(CallExpr *callexpr) {
        mStack.back().setPC(callexpr);
        FunctionDecl *callee = callexpr->getDirectCallee();
        if (callee == mInput) {
            int val;
#ifndef ASSIGNMENT_DEBUG
            llvm::errs() << "Please Input an Integer Value : ";
#endif
            scanf("%d", &val);
            mStack.back().bindStmt(callexpr, val);
        } else if (callee == mOutput) {
            Expr *outputExp = callexpr->getArg(0);
            int val = mStack.back().getStmtVal(outputExp);
#ifndef ASSIGNMENT_DEBUG
            llvm::errs() << val;
#else
            printf("%d\n", val);
#endif
        } else if (callee == mMalloc) {
            Expr *chunkSizeExp = callexpr->getArg(0);
            int chunkSize = mStack.back().getStmtVal(chunkSizeExp);
            int chunkVMAddr = heap.allocate(chunkSize);
            mStack.back().bindStmt(callexpr, chunkVMAddr);
        } else if (callee == mFree) {
            Expr *chunkVMAddrExp = callexpr->getArg(0);
            int chunkVMAddr = mStack.back().getStmtVal(chunkVMAddrExp);
            heap.release(chunkVMAddr);
        } else {
            /// You could add your code here for Function call Return
        }
    }

    void expr(Expr *expr) {

    }
};


