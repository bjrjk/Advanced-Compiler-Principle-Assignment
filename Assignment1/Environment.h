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

class StaticStorage {
private:
    map<Decl *, int> globData;
public:
    void set(Decl *varDecl, int val) {
        globData[varDecl] = val;
    }

    int get(Decl *varDecl) {
        assert(globData.count(varDecl) != 0);
        return globData[varDecl];
    }

    bool has(Decl *varDecl) const {
        return globData.count(varDecl);
    }
};


// Heap maps address to a value
class Heap {
private:
    struct chunkMeta {
    private:
        uint_t begin; // in bytes
        uint_t length, capacity; // in bytes
        char *pointer; // real pointer in host
    public:
        chunkMeta(uint_t length, uint_t &addressAccumulator) {
            this->begin = addressAccumulator;;
            this->length = length = (length << 2); // patch here to prevent from char treated as int leading to OOB
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

    chunkMeta *queryChunkMeta(int addr) const {
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
    static Heap *allocator;

    Heap() : addressAccumulator(0) {
        assert(allocator == nullptr);
        allocator = this;
    }

    ~Heap() {
        for_each(chunks.begin(), chunks.end(), [](chunkMeta *&chunk) {
            delete chunk;
            chunk = nullptr;
        });
        chunks.clear();
        allocator = nullptr;
    }

    int allocate(int size) {
        chunkMeta *chunk = new chunkMeta(size, addressAccumulator);
        chunks.push_back(chunk);
        return chunk->getBegin();
    }

    void release(int addr) {
        // Remove only when `addr` is chunk's begin address
        chunks.erase(remove_if(chunks.begin(), chunks.end(), [&](chunkMeta *&chunk) {
            if (chunk->getBegin() == addr) {
                delete chunk;
                chunk = nullptr;
                return true;
            }
            return false;
        }));
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


class StackFrame {
private:
    // StackFrame maps Variable Declaration to Value
    // Which are either integer or addresses (also represented using an Integer value)
    std::map<Decl *, int> mVars;
    std::map<Stmt *, int> mExprs;
    // The current stmt
    Stmt *mPC;
    int mRetVal;
    bool mHasRetVal;
public:
    StackFrame() : mVars(), mExprs(), mPC(), mRetVal(0), mHasRetVal(false) {}

    ~StackFrame() {
        for_each(mVars.begin(), mVars.end(), [&](pair<Decl *const, int> item) {
            if (VarDecl *varDecl = dyn_cast<VarDecl>(item.first)) {
                // For auto array, do heap release automatically
                if (varDecl->getType()->isConstantArrayType()) {
                    int heapAddr = item.second;
                    Heap::allocator->release(heapAddr);
                }
            }
        });
    }

    void bindDecl(Decl *decl, int val) {
        mVars[decl] = val;
    }

    int getDeclVal(Decl *decl) {
        assert(mVars.find(decl) != mVars.end());
        return mVars.find(decl)->second;
    }

    bool hasDeclVal(Decl *decl) const {
        return mVars.count(decl);
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
        mHasRetVal = true;
    }

    int getRetVal() const {
        return mRetVal;
    }

    bool hasRetVal() const {
        return mHasRetVal;
    }
};

class Environment {
private:
    InterpreterVisitor *iVisitor;

    Heap dHeap;
    vector<StackFrame> dStack;
    StaticStorage dStaticData;

    FunctionDecl *fFree;        // Declarations to the built-in functions
    FunctionDecl *fMalloc;
    FunctionDecl *fInput;
    FunctionDecl *fOutput;
    FunctionDecl *fEntry;       // Program entrypoint

public:
    Environment() : fFree(nullptr), fMalloc(nullptr),
                    fInput(nullptr), fOutput(nullptr), fEntry(nullptr) {}


    // Initialize the Environment
    void init(TranslationUnitDecl *unit, InterpreterVisitor *visitor) {
        iVisitor = visitor;
        // Prevent `dStack` vector from reallocating thus automatically freeing auto array on heap won't happen,
        // meanwhile the stack depth is limited to 1024.
        dStack.reserve(1024);
        // Create initialization stack frame
        dStack.emplace_back();
        // Do initialization
        for (TranslationUnitDecl::decl_iterator i = unit->decls_begin(), e = unit->decls_end(); i != e; ++i) {
            if (FunctionDecl *fDecl = dyn_cast<FunctionDecl>(*i)) {
                // Collect the Declarations to the built-in function pointers
                if (fDecl->getName().equals("FREE")) fFree = fDecl;
                else if (fDecl->getName().equals("MALLOC")) fMalloc = fDecl;
                else if (fDecl->getName().equals("GET")) fInput = fDecl;
                else if (fDecl->getName().equals("PRINT")) fOutput = fDecl;
                else if (fDecl->getName().equals("main")) fEntry = fDecl;
#ifdef ASSIGNMENT_DEBUG_DUMP
                if (fDecl->getDefinition() == nullptr) {
                    fprintf(stderr, "[+] Function prototype %s on %p.\n",
                            fDecl->getDeclName().getAsString().c_str(), fDecl);
                } else {
                    fprintf(stderr, "[+] Function declaration %s on %p, definition on %p.\n",
                            fDecl->getDeclName().getAsString().c_str(), fDecl, fDecl->getDefinition());
                }
#endif
            } else if (VarDecl *vDecl = dyn_cast<VarDecl>(*i)) {
                // Collect global variables and their init values
                int initVal = 0;
                if (vDecl->hasInit()) {
                    Expr *initExpr = vDecl->getInit();
                    iVisitor->Visit(initExpr);
                    initVal = dStack.back().getStmtVal(initExpr);
                }
                dStaticData.set(vDecl, initVal);
#ifdef ASSIGNMENT_DEBUG_DUMP
                fprintf(stderr, "[+] Global variable %s=%d on %p.\n",
                        vDecl->getDeclName().getAsString().c_str(), initVal, vDecl);
#endif
            }
        }
        // Pop the initialization stack frame
        dStack.pop_back();
        // Create stack frame for main
        dStack.emplace_back();
#ifdef ASSIGNMENT_DEBUG_DUMP
        fprintf(stderr, "[*] Entering entrypoint main on %p.\n", fEntry);
#endif
    }

    void bindDecl(Decl *decl, int val) {
        if (dStack.back().hasDeclVal(decl)) {
            dStack.back().bindDecl(decl, val);
        } else if (dStaticData.has(decl)) {
            dStaticData.set(decl, val);
        } else {
            assert(false);
        }
    }

    int getDeclVal(Decl *decl) {
        if (dStack.back().hasDeclVal(decl)) {
            return dStack.back().getDeclVal(decl);
        } else if (dStaticData.has(decl)) {
            return dStaticData.get(decl);
        } else {
            assert(false);
            return 0;
        }
    }

    FunctionDecl *getEntry() {
        return fEntry;
    }

    void integerLiteral(IntegerLiteral *intLiteral) {
        int literalVal = intLiteral->getValue().getSExtValue();
        dStack.back().bindStmt(intLiteral, literalVal);
    }

    void binaryOperator(BinaryOperator *bop) {
        Expr *LHSExpr = bop->getLHS();
        Expr *RHSExpr = bop->getRHS();
        auto opStr = bop->getOpcodeStr();

        if (opStr.equals("=")) { // Assignment
            int RHSVal = dStack.back().getStmtVal(RHSExpr);
            if (DeclRefExpr *declRefLHSExpr = dyn_cast<DeclRefExpr>(LHSExpr)) {
                Decl *decl = declRefLHSExpr->getFoundDecl();
                bindDecl(decl, RHSVal); // LHSValue of Assignment maybe global or local variable
            } else if (ArraySubscriptExpr *arrSubExpr = dyn_cast<ArraySubscriptExpr>(LHSExpr)) {
                int LHSAddr = dStack.back().getStmtVal(LHSExpr);
                Heap::allocator->set(LHSAddr, RHSVal);
            } else if (UnaryOperator *uop = dyn_cast<UnaryOperator>(LHSExpr)) {
                if (uop->getOpcodeStr(uop->getOpcode()).equals("*")) { // dereference
                    int LHSAddr = dStack.back().getStmtVal(LHSExpr);
                    Heap::allocator->set(LHSAddr, RHSVal);
                }
            }
            dStack.back().bindStmt(bop, RHSVal);
        } else { // Integer Arithmatic, Integer Comparative, Pointer Arithmatic
            int LHSVal = dStack.back().getStmtVal(LHSExpr);
            int RHSVal = dStack.back().getStmtVal(RHSExpr);
            int result;
            if (opStr.equals("+")) {
                if (bop->getType()->isIntegerType()) {
                    result = LHSVal + RHSVal;
                } else if (bop->getType()->isPointerType()) {
                    // TODO: Now consider all pointers as int *
                    if (LHSExpr->getType()->isPointerType() && RHSExpr->getType()->isIntegerType()) {
                        result = LHSVal + sizeof(int) * RHSVal;
                    } else if (LHSExpr->getType()->isIntegerType() && RHSExpr->getType()->isPointerType()) {
                        result = RHSVal + sizeof(int) * LHSVal;
                    } else {
                        assert(false);
                    }
                }
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

    void unaryOperator(UnaryOperator *uop) {
        Expr *subExpr = uop->getSubExpr();
        int subVal = dStack.back().getStmtVal(subExpr);
        auto opStr = uop->getOpcodeStr(uop->getOpcode());

        int result;
        if (opStr.equals("-")) {
            result = -subVal;
        } else if (opStr.equals("*")) {
            result = subVal; // Still store address here
        } else if (opStr.equals("++")) {
            result = ++subVal;
            if (DeclRefExpr *declRefExpr = dyn_cast<DeclRefExpr>(subExpr)) {
                Decl *decl = declRefExpr->getFoundDecl();
                dStack.back().bindDecl(decl, subVal);
            }
            dStack.back().bindStmt(subExpr, result);
        } else {
            assert(false);
            result = 0;
        }
        dStack.back().bindStmt(uop, result);
    }

    void unaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr *UoTTexpr) {
        if (UoTTexpr->getKind() == clang::UETT_SizeOf) {
            if (UoTTexpr->getArgumentType()->isIntegerType() ||
                UoTTexpr->getArgumentType()->isPointerType()) {
                dStack.back().bindStmt(UoTTexpr, sizeof(int));
            }
        }
    }

    void arraySubscriptExpr(ArraySubscriptExpr *arrSubExpr) {
        Expr *baseExpr = arrSubExpr->getBase(), *idxExpr = arrSubExpr->getIdx();
        int baseHeapAddr = dStack.back().getStmtVal(baseExpr);
        int elementOffset = dStack.back().getStmtVal(idxExpr);
        int targetAddr = -1;
        if (baseExpr->getType()->isPointerType()) {
            auto pointeeType = baseExpr->getType()->getPointeeOrArrayElementType();
            if (pointeeType->isIntegerType() || pointeeType->isPointerType()) {
                targetAddr = baseHeapAddr + elementOffset * sizeof(int);
            }
        }
        assert(targetAddr != -1);
        dStack.back().bindStmt(arrSubExpr, targetAddr);
    }

    void declStmt(DeclStmt *declstmt) {
        for (DeclStmt::decl_iterator it = declstmt->decl_begin(), ie = declstmt->decl_end();
             it != ie; ++it) {
            Decl *decl = *it;
            if (VarDecl *vardecl = dyn_cast<VarDecl>(decl)) {
                int initVal = 0;
                if (vardecl->getType()->isIntegerType()) {
                    if (vardecl->hasInit()) {
                        Expr *initExpr = vardecl->getInit();
                        iVisitor->Visit(initExpr);
                        initVal = dStack.back().getStmtVal(initExpr);
                    }
#ifdef ASSIGNMENT_DEBUG_DUMP
                    fprintf(stderr, "[+] Local int variable %s=%d on %p.\n",
                            vardecl->getDeclName().getAsString().c_str(), initVal, vardecl);
#endif
                } else if (vardecl->getType()->isConstantArrayType()) {
                    const ConstantArrayType *constArrType = dyn_cast<ConstantArrayType>(vardecl->getType());
                    unsigned int arrLength = constArrType->getSize().getZExtValue();
                    int heapAddr = Heap::allocator->allocate(arrLength * sizeof(int));
                    initVal = heapAddr;
#ifdef ASSIGNMENT_DEBUG_DUMP
                    fprintf(stderr, "[+] Local array %s[%u] at VMHeapAddr 0x%x, size %lu, on %p.\n",
                            vardecl->getDeclName().getAsString().c_str(), arrLength,
                            heapAddr, arrLength * sizeof(int), vardecl);
#endif
                } else if (vardecl->getType()->isPointerType()) {
#ifdef ASSIGNMENT_DEBUG_DUMP
                    fprintf(stderr, "[+] Local pointer %s on %p.\n",
                            vardecl->getDeclName().getAsString().c_str(), vardecl);
#endif
                }
                dStack.back().bindDecl(vardecl, initVal); // Local variables are on the stack frame
            }

        }
    }

    void declRefExpr(DeclRefExpr *declRefExpr) {
        // Variable maybe global or on the stack frame
        dStack.back().setPC(declRefExpr);
        auto declRefExprType = declRefExpr->getType();
        if (declRefExprType->isIntegerType() ||
            declRefExprType->isPointerType() && !declRefExprType->isFunctionPointerType() ||
            declRefExprType->isArrayType()) {
            Decl *decl = declRefExpr->getFoundDecl();
            int val = getDeclVal(decl);
            dStack.back().bindStmt(declRefExpr, val);
        }
    }

    void castExpr(CastExpr *castExpr) {
        dStack.back().setPC(castExpr);
        auto castExprType = castExpr->getType();
        if (castExprType->isIntegerType() ||
            castExprType->isPointerType() && !castExprType->isFunctionPointerType() ||
            castExprType->isArrayType()) {
            Expr *subExpr = castExpr->getSubExpr();
            int val = dStack.back().getStmtVal(subExpr);
            if (castExpr->getCastKind() == clang::CK_LValueToRValue) {
                if (ArraySubscriptExpr *arrSubExpr = dyn_cast<ArraySubscriptExpr>(subExpr)) {
                    val = Heap::allocator->get(val);
                } else if (UnaryOperator *uop = dyn_cast<UnaryOperator>(subExpr)) {
                    if (uop->getOpcodeStr(uop->getOpcode()).equals("*")) { // dereference
                        val = Heap::allocator->get(val);
                    }
                }
            }
            dStack.back().bindStmt(castExpr, val);
        }
    }

    void callExpr(CallExpr *callexpr) {
        dStack.back().setPC(callexpr);
        FunctionDecl *callee = callexpr->getDirectCallee();
#ifdef ASSIGNMENT_DEBUG_DUMP
        fprintf(stderr, "[*] Calling function: %s on %p, definition on %p.\n", callee->getName().bytes_begin(), callee,
                callee->getDefinition());
#endif
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
            // Get real definition instead of prototype or unable to visit its statement & its variables
            callee = callee->getDefinition();
            // Create new call stack
            dStack.emplace_back();
#define oldFrame (dStack.end() - 2)
#define newFrame (dStack.end() - 1)
            // Copy argument values to corresponding parameter bindings
            uint_t paramCount = callee->getNumParams();
            for (int i = 0; i < paramCount; i++) {
                Expr *argExpr = callexpr->getArg(i);
                int argVal = oldFrame->getStmtVal(argExpr);
                ParmVarDecl *paramDecl = callee->getParamDecl(i);
                newFrame->bindDecl(paramDecl, argVal); // Parameters are on the stack frame
#ifdef ASSIGNMENT_DEBUG_DUMP
                fprintf(stderr, "\t- Function parameter %d on %p: %s=%d.\n", i, paramDecl,
                        paramDecl->getName().bytes_begin(), argVal);
#endif
            }
            // Visit new function
            iVisitor->VisitStmt(callee->getBody());
            // Collect return value
            int retVal = newFrame->getRetVal();
            oldFrame->bindStmt(callexpr, retVal);
#undef oldFrame
#undef newFrame
            // Pop call stack
            dStack.pop_back();
        }
    }

    void parenExpr(ParenExpr *parenExpr) {
        Expr *subExpr = parenExpr->getSubExpr();
        int val = dStack.back().getStmtVal(subExpr);
        dStack.back().bindStmt(parenExpr, val);
    }

    void expr(Expr *expr) {

    }

    void returnStmt(ReturnStmt *retStmt) {
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
        Stmt *initExpr = forStmt->getInit();
        Expr *condExpr = forStmt->getCond();
        if (initExpr) {
            iVisitor->Visit(forStmt->getInit());
        }

        while (true) {
            iVisitor->Visit(condExpr);
            int condVal = dStack.back().getStmtVal(condExpr);
            if (condVal == 0) break;
            iVisitor->Visit(forStmt->getBody());
            iVisitor->Visit(forStmt->getInc());
        }
    }

    void stmt(Stmt *stmt) {
        for (auto *SubStmt: stmt->children()) {
            if (dStack.back().hasRetVal()) break; // Stop from executing current function after return statement
            if (SubStmt) {
                iVisitor->Visit(SubStmt);
            }
        }
    }
};
