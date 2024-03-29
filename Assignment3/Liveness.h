#include <llvm/IR/Function.h>
#include <llvm/Pass.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/IntrinsicInst.h>

#include "Dataflow.h"

using namespace llvm;


struct LivenessFact {
    std::set<Instruction *> LiveVars;             /// Set of variables which are live
    LivenessFact() : LiveVars() {}

    LivenessFact(const LivenessFact &info) : LiveVars(info.LiveVars) {}

    bool operator==(const LivenessFact &info) const {
        return LiveVars == info.LiveVars;
    }
};

inline raw_ostream &operator<<(raw_ostream &out, const LivenessFact &info) {
    for (std::set<Instruction *>::iterator ii = info.LiveVars.begin(); ii != info.LiveVars.end(); ++ii) {
        const Instruction *inst = *ii;
        out << inst->getName();
        out << " ";
    }
    return out;
}


class LivenessVisitor : public DataflowVisitor<struct LivenessFact> {
public:
    LivenessVisitor() {}

    void merge(LivenessFact *dest, const LivenessFact &src) override {
        for (std::set<Instruction *>::const_iterator ii = src.LiveVars.begin(); ii != src.LiveVars.end(); ++ii) {
            dest->LiveVars.insert(*ii);
        }
    }

    void transferInst(Instruction *inst, LivenessFact *inputDFVal) override {
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


class Liveness : public FunctionPass {
public:

    static char ID;

    Liveness() : FunctionPass(ID) {}

    bool runOnFunction(Function &F) override {
        F.dump();
        LivenessVisitor visitor;
        DataflowResult<LivenessFact>::Type result;
        LivenessFact initVal;

        analyzeBackward(&F, &visitor, &result, initVal);
        printDataflowResult<LivenessFact>(errs(), result);
        return false;
    }
};



