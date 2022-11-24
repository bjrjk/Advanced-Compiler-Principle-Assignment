#include <llvm/IR/Function.h>
#include <llvm/Pass.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/IntrinsicInst.h>

#include "Dataflow.h"

using namespace llvm;


struct PointerAnalysisFact {
    std::set<Instruction *> LiveVars;             /// Set of variables which are live
    PointerAnalysisFact() : LiveVars() {}

    PointerAnalysisFact(const PointerAnalysisFact &info) : LiveVars(info.LiveVars) {}

    bool operator==(const PointerAnalysisFact &info) const {
        return LiveVars == info.LiveVars;
    }
};

inline raw_ostream &operator<<(raw_ostream &out, const PointerAnalysisFact &info) {
    for (std::set<Instruction *>::iterator ii = info.LiveVars.begin(); ii != info.LiveVars.end(); ++ii) {
        const Instruction *inst = *ii;
        out << inst->getName();
        out << " ";
    }
    return out;
}


class PointerAnalysisVisitor : public DataflowVisitor<struct PointerAnalysisFact> {
public:
    PointerAnalysisVisitor() {}

    void merge(PointerAnalysisFact *dest, const PointerAnalysisFact &src) override {
        for (std::set<Instruction *>::const_iterator ii = src.LiveVars.begin(); ii != src.LiveVars.end(); ++ii) {
            dest->LiveVars.insert(*ii);
        }
    }

    void transferInst(Instruction *inst, PointerAnalysisFact *inputDFVal) override {
        if (isa<DbgInfoIntrinsic>(inst)) return;
        inputDFVal->LiveVars.erase(inst);
        for (User::op_iterator oi = inst->op_begin(), oe = inst->op_end();
             oi != oe; ++oi) {
            Value *val = *oi;
            if (isa<Instruction>(val))
                inputDFVal->LiveVars.insert(cast<Instruction>(val));
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

        analyzeBackward(&F, &visitor, &result, initVal);
        printDataflowResult<PointerAnalysisFact>(errs(), result);
        return false;
    }
};



