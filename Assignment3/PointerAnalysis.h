#include <algorithm>
#include <map>
#include <set>
#include <string>

#include <llvm/IR/Function.h>
#include <llvm/Pass.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instructions.h>

#include "Dataflow.h"

using namespace llvm;

typedef Value Object_t;
typedef Value Pointer_t;

static inline bool isObject(Object_t *maybeObject) {
    return isa<Instruction>(maybeObject) || isa<Argument>(maybeObject) || isa<Function>(maybeObject);
}

static inline bool isPointer(Pointer_t *maybePointer) {
    return isObject(maybePointer) || maybePointer->getType()->isPointerTy() &&
                                     (isa<Instruction>(maybePointer) || isa<Function>(maybePointer));
}

static inline void assertIsObject(Object_t *maybeObject) {
    assert(isObject(maybeObject));
}

static inline void assertIsPointer(Pointer_t *maybePointer) {
    assert(isPointer(maybePointer));
}

class PointerAnalysisFact {
private:
    std::set<Pointer_t *> pointerContainer;
    std::set<Object_t *> objectContainer;
    std::map<Pointer_t *, std::set<Object_t *>> pointToSetContainer;
    std::set<Pointer_t *> initializedPointerContainer;
public:
    PointerAnalysisFact() = default;

    PointerAnalysisFact(const PointerAnalysisFact &info) = default;

    bool operator==(const PointerAnalysisFact &fact2) const {
        return pointerContainer == fact2.pointerContainer &&
               objectContainer == fact2.objectContainer &&
               pointToSetContainer == fact2.pointToSetContainer;
    }

    inline bool addPointer(Pointer_t *pointer) {
        assertIsPointer(pointer);
        bool flag = false;

        flag |= pointerContainer.insert(pointer).second;
        flag |= objectContainer.insert(pointer).second; // A pointer is an object
        if (flag)
            pointToSetContainer[pointer].insert(pointer).second;
        // A pointer treated as object should point to itself

        return flag;
    }

    inline bool addObject(Object_t *object) {
        assertIsObject(object);
        bool flag = false;

        flag |= objectContainer.insert(object).second;
        flag |= pointerContainer.insert(object).second; // To process point-to relation, we treat object as pointer
        if (flag)
            pointToSetContainer[object].insert(object).second;
        // When initializing, an object treated as pointer should point to itself

        return flag;
    }

    bool addObjectSet(const std::set<Object_t *> &objectSet) {
        bool flag = false;
        for_each(objectSet.begin(), objectSet.end(), [&](auto *object) {
            flag |= addObject(object);
        });
        return flag;
    }

    bool addPointTo(Pointer_t *pointer, Object_t *object) {
        // The `pointer` must be pointer type. However, the `object` can be either a pointer or value type.
        addPointer(pointer);
        addObject(object);

        return pointToSetContainer[pointer].insert(object).second;
        // Reference: https://zh.cppreference.com/w/cpp/container/set/insert
    }

    bool unionPointToSet(Pointer_t *pointer, const std::set<Object_t *> &externalObjectSet) {
        addPointer(pointer);
        addObjectSet(externalObjectSet);

        std::set<Object_t *> tmpUnionSet;
        auto &internalObjectSet = pointToSetContainer[pointer];

        std::set_union(internalObjectSet.begin(), internalObjectSet.end(),
                       externalObjectSet.begin(), externalObjectSet.end(),
                       std::inserter(tmpUnionSet, tmpUnionSet.end()));

        bool flag = (tmpUnionSet == internalObjectSet);
        if (!flag) std::swap(tmpUnionSet, internalObjectSet);
        return flag;
    }

    bool unionAllPointToSet(const std::set<Object_t *> &externalObjectSet) {
        bool flag = false;

        for (auto &PTSKeyPair: pointToSetContainer) {
            flag |= unionPointToSet(PTSKeyPair.first, externalObjectSet);
        }

        return flag;
    }

    void clearPointToSet(Pointer_t *pointer) {
        addPointer(pointer);

        pointToSetContainer[pointer].clear();
    }

    const std::set<Pointer_t *> &getPointerSet() const {
        return pointerContainer;
    }

    const std::set<Object_t *> &getObjectSet() const {
        return objectContainer;
    }

    const std::set<Object_t *> &getPointToSet(Pointer_t *pointer) {
        addPointer(pointer);
        return pointToSetContainer[pointer];
    }

    const std::set<Object_t *> *getPointToSet(Pointer_t *pointer) const {
        assertIsPointer(pointer);
        if (pointToSetContainer.count(pointer) != 0)
            return &pointToSetContainer.find(pointer)->second;
        else
            return nullptr;
    }

    std::set<Object_t *> getPointToSet(const std::set<Object_t *> &maybePointerSet) {
        std::set<Object_t *> resultSet;
        for (auto *maybePointer: maybePointerSet) {
            if (!isPointer(maybePointer)) continue;
            auto &thisPTS = getPointToSet(maybePointer);
            resultSet.insert(thisPTS.begin(), thisPTS.end());
        }
        return std::move(resultSet);
    }

    bool unionFact(const PointerAnalysisFact &src) {
        bool flag = false;

        for (auto &externalPTSPair: src.pointToSetContainer) {
            flag |= unionPointToSet(externalPTSPair.first, externalPTSPair.second);
        }

        return flag;
    }

    void setTop() {
        for (auto *pointer: pointerContainer) {
            pointToSetContainer[pointer] = objectContainer;
        }
    }

    inline bool trySetPointerInitialized(Pointer_t *pointer) {
        if (!isa<AllocaInst>(pointer)) return false;
        // When the pointer isn't initialized, add pointer to initializedPointerContainer and return true;
        // When the pointer got initialized (pointer in initializedPointerContainer), return false.
        if (!initializedPointerContainer.count(pointer)) {
            initializedPointerContainer.insert(pointer);
            return true;
        }
        return false;
    }
};

inline raw_ostream &operator<<(raw_ostream &out, const PointerAnalysisFact &info) {
    char buf[1024]; // Warning: Buffer Overflow
    int counter;

    counter = 0;
    out << "\t\t[!] Pointer Container: \n";
    for (auto *pointer: info.getPointerSet()) {
        sprintf(buf, "%15s(%p)\t\t\t\t", pointer->getName().data(), pointer);
        out << buf;
        if (counter == 3) out << "\n";
        counter = (counter + 1) % 4;
    }
    out << "\n";

    counter = 0;
    out << "\t\t[!] Object Container: \n";
    for (auto *object: info.getObjectSet()) {
        sprintf(buf, "%15s(%p)\t\t\t\t", object->getName().data(), object);
        out << buf;
        if (counter == 3) out << "\n";
        counter = (counter + 1) % 4;
    }
    out << "\n";

    out << "\t\t[!] Point-to Relation: \n";
    for (auto *pointer: info.getPointerSet()) {
        auto *PTS = info.getPointToSet(pointer);
        if (PTS == nullptr) continue;
        sprintf(buf, "\t\t\t[--] Pointer: %s(%p), Pointee: \n", pointer->getName().data(), pointer);
        out << buf;
        counter = 0;
        for (auto *object: *PTS) {
            sprintf(buf, "%15s(%p)\t\t\t\t", object->getName().data(), object);
            out << buf;
            if (counter == 3) out << "\n";
            counter = (counter + 1) % 4;
        }
        out << "\n";
    }

    return out;
}


class PointerAnalysisVisitor : public DataflowVisitor<struct PointerAnalysisFact> {
public:
    PointerAnalysisVisitor() {}

    void merge(PointerAnalysisFact *dest, const PointerAnalysisFact &src) override {
        dest->unionFact(src);
    }

    static inline void transferFactReference(PointerAnalysisFact *fact,
                                             Pointer_t *LHS, Object_t *RHS) { // LHS = &RHS
        assertIsPointer(LHS);
        assertIsObject(RHS);

        fact->clearPointToSet(LHS);
        fact->addPointTo(LHS, RHS);
    }

    static inline void transferFactAssign(PointerAnalysisFact *fact,
                                          Pointer_t *LHS, Pointer_t *RHS) { // LHS = RHS
        assertIsPointer(LHS);
        assertIsPointer(RHS);

        auto RHS_PTS = fact->getPointToSet(RHS);
        fact->clearPointToSet(LHS);
        fact->unionPointToSet(LHS, RHS_PTS);
    }

    static inline void transferFactLoad(PointerAnalysisFact *fact,
                                        Pointer_t *LHS, Pointer_t *RHS) { // LHS = *RHS
        assertIsPointer(LHS);
        assertIsPointer(RHS);

        auto &RHS_PTS = fact->getPointToSet(RHS);
        auto RHS_PTS_PTS = fact->getPointToSet(RHS_PTS);
        fact->clearPointToSet(LHS);
        fact->unionPointToSet(LHS, RHS_PTS_PTS);
    }

    static inline void transferFactStore(PointerAnalysisFact *fact,
                                         Pointer_t *LHS, Pointer_t *RHS) { // *LHS = RHS
        assertIsPointer(LHS);
        assertIsPointer(RHS);

        auto &LHS_PTS = fact->getPointToSet(LHS);
        switch (LHS_PTS.size()) {
            case 0: {
                fact->setTop();
                break;
            }
            case 1: {
                Pointer_t *onlyPointee = *LHS_PTS.begin();
                transferFactAssign(fact, onlyPointee, RHS);
                break;
            }
            default: {
                auto RHS_PTS = fact->getPointToSet(RHS);
                fact->unionAllPointToSet(RHS_PTS);
                break;
            }
        }
    }

    static Instruction *createNewInst(Value *parent, const Twine &name) {
        Instruction *field = new AllocaInst(IntegerType::get(parent->getContext(), 32), 0);
        field->setName(name);
        return field;
    }

    static Instruction *createStructField(Value *parent) {
        // Add unified field object
        return createNewInst(parent, parent->getName() + ".field");
    }

    void transferInstAlloca(AllocaInst *allocaInst, PointerAnalysisFact *fact) {
        // Heap Abstraction: Allocation Site
        if (allocaInst->getAllocatedType()->isPointerTy()) {
            // Pointer Allocation
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr, "\t\t\t[-] Pointer Allocation.\n");
#endif
            fact->addPointer(allocaInst);
        } else if (allocaInst->getAllocatedType()->isStructTy()) {
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr, "\t\t\t[-] Struct Allocation.\n");
#endif
            fact->addObject(allocaInst);
            Instruction *field = createStructField(allocaInst);
            fact->addObject(field);
            transferFactReference(fact, allocaInst, field);
        } else {
            // Object Allocation
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr, "\t\t\t[-] Object Allocation.\n");
#endif
            fact->addObject(allocaInst);
        }
    }

    void transferInstLoad(LoadInst *loadInst, PointerAnalysisFact *fact) {
        auto *LHS = loadInst;
        auto *RHS = loadInst->getPointerOperand();
#ifdef ASSIGNMENT_DEBUG_DUMP
        fprintf(stderr,
                "\t\t\t[-] Transfer Fact of Load Operation: %s(%p) <- %s(%p).\n",
                LHS->getName().data(), LHS, RHS->getName().data(), RHS);
#endif
        transferFactLoad(fact, LHS, RHS);
    }

    void transferInstStore(StoreInst *storeInst, PointerAnalysisFact *fact) {
        auto *LHS = storeInst->getPointerOperand();
        auto *RHS = storeInst->getValueOperand();
        if (auto *argRHS = dyn_cast<Argument>(RHS)) {
            // Handle pointer-to relation of function argument
#ifdef INTRA_PROCEDURE_ANALYSIS
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr,
                    "\t\t\t[-] Transfer Fact of Store Operation (Function Argument Handling, INTRA_PROCEDURE_ANALYSIS): %s(%p) <- %s(%p).\n",
                    LHS->getName().data(), LHS, argRHS->getName().data(), argRHS);
#endif
            fact->addObject(argRHS);
            transferFactReference(fact, LHS, argRHS);

            // Mock for structure
            Type *rootType = [=]() {
                Type *curType = argRHS->getType();
                while (curType->isPointerTy())
                    curType = curType->getPointerElementType();
                return curType;
            }();

            if (rootType->isStructTy()) {
                Type *curType = argRHS->getType();
                Value *parent = argRHS;
                while (curType->isPointerTy()) {
                    curType = curType->getPointerElementType();
                    Instruction *mockObject = createNewInst(parent, parent->getName() + ".mock");
                    fact->addObject(mockObject);
                    transferFactReference(fact, parent, mockObject);
                    parent = mockObject;
                }
                Instruction *field = createStructField(parent);
                fact->addObject(field);
                transferFactReference(fact, parent, field);
            }
#else // INTER_PROCEDURE_ANALYSIS
            fprintf(stderr, "Inter-procedure analysis unimplemented!\n");
            assert(false);
#endif
        } else if (fact->trySetPointerInitialized(LHS)) {
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr,
                    "\t\t\t[-] Transfer Fact of Store Operation (Pointer Initialize, Assign): %s(%p) <- %s(%p).\n",
                    LHS->getName().data(), LHS, RHS->getName().data(), RHS);
#endif
            // Handle pointer initialize
            transferFactAssign(fact, LHS, RHS);
        } else {
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr, "\t\t\t[-] Transfer Fact of Store Operation (Normal Variable, Store): %s(%p) <- %s(%p).\n",
                    LHS->getName().data(), LHS, RHS->getName().data(), RHS);
#endif
            transferFactStore(fact, LHS, RHS);
        }
    }

    void transferInstGetElemPtr(GetElementPtrInst *getElemPtrInst, PointerAnalysisFact *fact) {
        // `getelementptr %struct, src` is computing an address of structure's data field
        // As the analysis is field-insensitive, Equivalent to Assign Fact transfer
        auto *LHS = getElemPtrInst;
        auto *RHS = getElemPtrInst->getPointerOperand();
#ifdef ASSIGNMENT_DEBUG_DUMP
        fprintf(stderr,
                "\t\t\t[-] Transfer Fact of GetElementPtr(Assign) Operation: %s(%p) <- %s(%p).\n",
                LHS->getName().data(), LHS, RHS->getName().data(), RHS);
#endif
        transferFactAssign(fact, LHS, RHS);
    }

    void transferInstBitCast(BitCastInst *bitCastInst, PointerAnalysisFact *fact) {
        auto *LHS = bitCastInst;
        auto *RHS = bitCastInst->getOperand(0);
#ifdef ASSIGNMENT_DEBUG_DUMP
        fprintf(stderr,
                "\t\t\t[-] Transfer Fact of BitCast(Assign) Operation: %s(%p) <- %s(%p).\n",
                LHS->getName().data(), LHS, RHS->getName().data(), RHS);
#endif
        transferFactAssign(fact, LHS, RHS);
    }

    void transferInstCall(CallBase *callInst, PointerAnalysisFact *fact) {
        auto functionName = callInst->getCalledOperand()->getName();
        if (functionName.startswith("llvm.dbg")) {
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr,
                    "\t\t\t[-] Debug Call, return.\n");
#endif
            return;
        } else if (functionName.startswith("llvm.memcpy")) {
            auto *LHS = callInst->getOperand(0);
            auto *RHS = callInst->getOperand(1);
            // TODO: Implement copy semantic
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr,
                    "\t\t\t[-] Transfer Fact of llvm.memcpy(Assign) Operation: %s(%p) <- %s(%p).\n",
                    LHS->getName().data(), LHS, RHS->getName().data(), RHS);
#endif
            transferFactAssign(fact, LHS, RHS);
        }
    }

    void transferInst(Instruction *inst, PointerAnalysisFact *fact) override {
        if (auto *allocaInst = dyn_cast<AllocaInst>(inst)) {
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr, "\t\t[*] Handle %s instruction %p:", "AllocaInst", allocaInst);
            inst->dump();
#endif
            transferInstAlloca(allocaInst, fact);
        } else if (auto *storeInst = dyn_cast<StoreInst>(inst)) {
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr, "\t\t[*] Handle %s instruction %p:", "StoreInst", storeInst);
            inst->dump();
#endif
            transferInstStore(storeInst, fact);
        } else if (auto *loadInst = dyn_cast<LoadInst>(inst)) {
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr, "\t\t[*] Handle %s instruction %p:", "LoadInst", loadInst);
            inst->dump();
#endif
            transferInstLoad(loadInst, fact);
        } else if (auto *getElemPtrInst = dyn_cast<GetElementPtrInst>(inst)) {
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr, "\t\t[*] Handle %s instruction %p:", "GetElementPtrInst", getElemPtrInst);
            inst->dump();
#endif
            transferInstGetElemPtr(getElemPtrInst, fact);
        } else if (auto *bitCastInst = dyn_cast<BitCastInst>(inst)) {
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr, "\t\t[*] Handle %s instruction %p:", "BitCastInst", bitCastInst);
            inst->dump();
#endif
            transferInstBitCast(bitCastInst, fact);
        } else if (auto *callBase = dyn_cast<CallBase>(inst)) {
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr, "\t\t[*] Handle %s instruction %p:", "CallInst", callBase);
            inst->dump();
#endif
            transferInstCall(callBase, fact);
        }
#ifdef ASSIGNMENT_DEBUG_DUMP
        printDataflowFact<PointerAnalysisFact>(errs(), *fact);
#endif
    }
};


class PointerAnalysis : public FunctionPass {
public:
    static char ID;

    PointerAnalysis() : FunctionPass(ID) {}

    void labelAnonymousInstruction(Function &function) {
        char buf[1024]; // Warning: Buffer Overflow
        int counter = 0;

        for (auto &basicBlock: function) {
            for (auto &inst: basicBlock) {
                if (!inst.getType()->isVoidTy() && inst.getName().equals("")) {
                    sprintf(buf, "tmp_%d", counter++);
                    inst.setName(buf);
                }
            }
        }
    }

    bool runOnFunction(Function &F) override {
#ifdef ASSIGNMENT_DEBUG_DUMP
        labelAnonymousInstruction(F);
        fprintf(stderr, "[+] Analyzing Function %s %p, IR:\n", F.getName().data(), &F);
        stderrCyanBackground();
        F.dump();
        stderrNormalBackground();
#endif
        PointerAnalysisVisitor visitor;
        DataflowResult<PointerAnalysisFact>::Type result;
        PointerAnalysisFact initVal;

        analyzeForward(&F, &visitor, &result, initVal);
        printDataflowResult<PointerAnalysisFact>(errs(), result);
        return false;
    }
};



