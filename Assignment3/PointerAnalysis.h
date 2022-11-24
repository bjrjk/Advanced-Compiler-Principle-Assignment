#include <map>

#include <llvm/IR/Function.h>
#include <llvm/Pass.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/IntrinsicInst.h>

#include "Dataflow.h"

using namespace llvm;


class PointerAnalysisFact {
private:

public:
    PointerAnalysisFact() {}

    PointerAnalysisFact(const PointerAnalysisFact &info) {}

    bool operator==(const PointerAnalysisFact &info) const {

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



