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

typedef AllocaInst Object_t;
typedef AllocaInst Pointer_t;

inline static void assertIsPointer(Pointer_t *maybePointer) {
    assert(maybePointer->getType()->isPointerTy());
}

inline static bool isPointer(Pointer_t *maybePointer) {
    return maybePointer->getType()->isPointerTy();
}

class PointerAnalysisFact {
private:
    std::map<Pointer_t *, std::set<Object_t *>> pointToSetContainer;
public:
    PointerAnalysisFact() = default;

    PointerAnalysisFact(const PointerAnalysisFact &info) = default;

    bool operator==(const PointerAnalysisFact &fact2) const {
        return pointToSetContainer == fact2.pointToSetContainer;
    }

    bool addPointTo(Pointer_t *pointer, Object_t *object) {
        // The `pointer` must be pointer type. However, the `object` can be either a pointer or value type.
        assertIsPointer(pointer);
        return pointToSetContainer[pointer].insert(object).second;
        // Reference: https://zh.cppreference.com/w/cpp/container/set/insert
    }

    bool unionPointToSet(Pointer_t *pointer, const std::set<Object_t *> &externalObjectSet) {
        assertIsPointer(pointer);

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
        assertIsPointer(pointer);

        pointToSetContainer[pointer].clear();
    }

    std::set<Pointer_t *> &&getPointerSet() const {
        std::set<Pointer_t *> resultSet;
        for (auto &PTSKeyPair: pointToSetContainer) {
            resultSet.insert(PTSKeyPair.first);
        }
        return std::move(resultSet);
    }

    const std::set<Object_t *> &getPointToSet(Pointer_t *pointer) {
        assertIsPointer(pointer);
        return pointToSetContainer[pointer];
    }

    std::set<Object_t *> &&getPointToSet(const std::set<Object_t *> &maybePointerSet) {
        std::set<Object_t *> resultSet;
        for (auto *maybePointer: maybePointerSet) {
            if (!isPointer(maybePointer)) continue;
            auto &thisPTS = getPointToSet(maybePointer);
            resultSet.insert(thisPTS.begin(), thisPTS.end());
        }
        return std::move(resultSet);
    }
};

inline raw_ostream &operator<<(raw_ostream &out, const PointerAnalysisFact &info) {

    return out;
}


class PointerAnalysisVisitor : public DataflowVisitor<struct PointerAnalysisFact> {
public:
    PointerAnalysisVisitor() {}

    void merge(PointerAnalysisFact *dest, const PointerAnalysisFact &src) override {

    }

    void transferInst(Instruction *inst, PointerAnalysisFact *inputDFVal) override {
        if (auto *allocaInst = dyn_cast<AllocaInst>(inst)) {
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr, "\t- Process %s instruction:", "AllocaInst");
            inst->dump();
            fprintf(stderr, "\n");
#endif
        } else if (auto *storeInst = dyn_cast<StoreInst>(inst)) {
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr, "\t- Process %s instruction:", "StoreInst");
            inst->dump();
            fprintf(stderr, "\n");
#endif
        } else if (auto *loadInst = dyn_cast<LoadInst>(inst)) {
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr, "\t- Process %s instruction:", "LoadInst");
            inst->dump();
            fprintf(stderr, "\n");
#endif
        }

    }
};


class PointerAnalysis : public FunctionPass {
public:
    static char ID;

    PointerAnalysis() : FunctionPass(ID) {}

    bool runOnFunction(Function &F) override {
        F.dump();
        PointerAnalysisVisitor visitor;
        DataflowResult<PointerAnalysisFact>::Type result;
        PointerAnalysisFact initVal;

        analyzeForward(&F, &visitor, &result, initVal);
        //printDataflowResult<PointerAnalysisFact>(errs(), result);
        return false;
    }
};



