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
                auto &callGraph = dataflowResultContainer[&func][&basicBlock].output.getCallGraph();
                doCallSiteCollection(callGraph);
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
                auto &callGraph = dataflowResultContainer[&entrypoint][&basicBlock].output.getCallGraph();
                doCallSiteCollection(callGraph);
            }
        }
    }

    void doCallSiteCollection(const std::map<Value *, std::set<Value *>> &callGraph) {
        for (auto &callEdge: callGraph) {
            auto &callBase = callEdge.first;
            auto &calledFunctionSet = callEdge.second;
            if (auto *realCallBase = dyn_cast<CallBase>(callBase)) { // Handle Source Node of Type CallBase
                for (auto *calledFunction: calledFunctionSet) {
                    if (auto *realCalledFunction = dyn_cast<Function>(calledFunction)) { // Handle CallBase -> Function
                        callSiteContainer[realCallBase->getDebugLoc().getLine()].insert(realCalledFunction->getName());
                    } else if (callGraph.count(calledFunction)) { // Handle CallBase -> FunctionPtr
                        for (auto *realCalledFunctionUncasted: callGraph.find(calledFunction)->second) { // Traverse FunctionPtr
                            auto *realCalledFunction = cast<Function>(realCalledFunctionUncasted); // Function
                            callSiteContainer[realCallBase->getDebugLoc().getLine()].insert(realCalledFunction->getName());
                        }
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


