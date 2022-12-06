#pragma once

#include "PointerAnalysis.h"

class InterAnalysis : public ModulePass {
private:
    std::map<Function *, DataflowResult<PointerAnalysisFact>::Type> dataflowResultContainer;
    std::map<unsigned int, std::set<std::string>> callSiteContainer;

public:
    static char ID;
    InterAnalysis() : ModulePass(ID) {}

    void runIntraProcedureAnalysis(Module &M) {
        PointerAnalysisVisitor visitor;
        PointerAnalysisFact initVal;

        for (auto &func: M) {
            PointerAnalysis::analyzeFunction(func, &visitor, &dataflowResultContainer[&func], initVal, true);
        }
    }

    void collectIntraCallSiteResult(Module &M) {
        for (auto &func: M) {
            for (auto &basicBlock: func) {
                for (auto &callEdge: dataflowResultContainer[&func][&basicBlock].output.getCallGraph()) {
                    auto &callBase = callEdge.first;
                    auto &calledFunctionSet = callEdge.second;
                    for (auto *calledFunction: calledFunctionSet) {
                        callSiteContainer[callBase->getDebugLoc().getLine()].insert(calledFunction->getName());
                    }
                }
            }
        }
    }

    void runInterProcedureAnalysis(Function &entrypoint) {
        PointerAnalysisVisitor visitor;
        DataflowResult<PointerAnalysisFact>::Type result;
        PointerAnalysisFact initVal;

        PointerAnalysis::analyzeFunction(entrypoint, &visitor, &dataflowResultContainer[&entrypoint], initVal, true);
    }

    void collectInterCallSiteResult(Module &M, Function &entrypoint) {
        for (auto &func: M) {
            for (auto &basicBlock: func) {
                for (auto &callEdge: dataflowResultContainer[&entrypoint][&basicBlock].output.getCallGraph()) {
                    auto &callBase = callEdge.first;
                    auto &calledFunctionSet = callEdge.second;
                    for (auto *calledFunction: calledFunctionSet) {
                        callSiteContainer[callBase->getDebugLoc().getLine()].insert(calledFunction->getName());
                    }
                }
            }
        }
    }

    void printCallSiteResult() {
        for_each(callSiteContainer.begin(), callSiteContainer.end(), [&](auto &lineCalleeNamePairs) {
            int lineNumber = lineCalleeNamePairs.first;
            std::set<std::string> &lineCallees = lineCalleeNamePairs.second;
            printf("%u :", lineNumber);
            bool flag = true;
            for (auto &maybeCalledFuncName: lineCallees) {
                printf(", %s" + flag, maybeCalledFuncName.data());
                flag = false;
            }
            printf("\n");
        });
    }

    bool runOnModule(Module &M) override {
#ifdef INTRA_PROCEDURE_ANALYSIS
        runIntraProcedureAnalysis(M);
        collectIntraCallSiteResult(M);
#else //INTER_PROCEDURE_ANALYSIS
        for (auto &func: M) {
            runInterProcedureAnalysis(func);
            collectInterCallSiteResult(M, func);
        }
#endif
        printCallSiteResult();
        return false;
    }
};


