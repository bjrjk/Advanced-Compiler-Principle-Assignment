#include <algorithm>
#include <map>
#include <set>

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
    return isa<AllocaInst>(maybeObject) || isa<Argument>(maybeObject);
}

static inline bool isPointer(Pointer_t *maybePointer) {
    return isObject(maybePointer) ||
           isa<AllocaInst>(maybePointer) && maybePointer->getType()->isPointerTy();
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

        return flag;
    }

    inline bool addObject(Object_t *object) {
        assertIsObject(object);
        bool flag = false;

        flag |= objectContainer.insert(object).second;
        flag |= pointerContainer.insert(object).second; // To process point-to relation, we treat object as pointer
        flag |= pointToSetContainer[object].insert(object).second; // An object treated as pointer should point to itself

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
        if (flag) std::swap(tmpUnionSet, internalObjectSet);
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
};

inline raw_ostream &operator<<(raw_ostream &out, const PointerAnalysisFact &info) {
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

    void transferInstAlloca(AllocaInst *allocaInst, PointerAnalysisFact *fact) {
        // Heap Abstraction: Allocation Site
        if (allocaInst->getAllocatedType()->isPointerTy()) {
            // Pointer Allocation
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr, "\t\t\t[-] Pointer Allocation.\n");
#endif
            fact->addPointer(allocaInst);
        } else {
            // Object Allocation
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr, "\t\t\t[-] Object Allocation.\n");
#endif
            fact->addObject(allocaInst);
        }
    }

    void transferInstLoad(LoadInst *loadInst, PointerAnalysisFact *fact) {

    }

    void transferInstStore(StoreInst *storeInst, PointerAnalysisFact *fact) {
        auto *LHS = storeInst->getPointerOperand();
        auto *RHS = storeInst->getValueOperand();
        if (auto *argRHS = dyn_cast<Argument>(RHS)) {
            // Handle pointer-to relation of function argument
#ifdef INTRA_PROCEDURE_ANALYSIS
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr, "\t\t\t[-] INTRA_PROCEDURE_ANALYSIS: Handle pointer-to relation of function argument: transferFactStore %s <- %s.\n",
                    LHS->getName().data(), argRHS->getName().data());
#endif
            transferFactStore(fact, LHS, argRHS);
#else // INTER_PROCEDURE_ANALYSIS
            fprintf(stderr, "Inter-procedure analysis unimplemented!\n");
            assert(false);
#endif
        }
    }

    void transferInstGetElemPtr(GetElementPtrInst *getElemPtrInst, PointerAnalysisFact *fact) {

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

        } else if (auto *getElemPtrInst = dyn_cast<GetElementPtrInst>(inst)) {
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr, "\t\t[*] Handle %s instruction %p:", "GetElementPtrInst", getElemPtrInst);
            inst->dump();
#endif

        }

    }
};


class PointerAnalysis : public FunctionPass {
public:
    static char ID;

    PointerAnalysis() : FunctionPass(ID) {}

    bool runOnFunction(Function &F) override {
#ifdef ASSIGNMENT_DEBUG_DUMP
        fprintf(stderr, "[+] Analyzing Function %s %p, IR:\n", F.getName().data(), &F);
        stderrCyanBackground();
        F.dump();
        stderrNormalBackground();
#endif
        PointerAnalysisVisitor visitor;
        DataflowResult<PointerAnalysisFact>::Type result;
        PointerAnalysisFact initVal;

        analyzeForward(&F, &visitor, &result, initVal);
        //printDataflowResult<PointerAnalysisFact>(errs(), result);
        return false;
    }
};



