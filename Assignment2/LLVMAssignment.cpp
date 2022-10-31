#include <llvm/Support/CommandLine.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Utils.h>
#include <llvm/IR/Function.h>
#include <llvm/Pass.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instructions.h>

#include <vector>
#include <map>
#include <utility>
#include <string>
#include <iterator>
#include <algorithm>

#include "util.hpp"

using namespace llvm;
using namespace std;

static ManagedStatic<LLVMContext> GlobalContext;

static LLVMContext &getGlobalContext() { return *GlobalContext; }

/* In LLVM 5.0, when -O0 passed to clang , the functions generated with clang will
 * have optnone attribute which would lead to some transform passes disabled, like mem2reg.
 */
struct EnableFunctionOptPass : public FunctionPass {
    static char ID;

    EnableFunctionOptPass() : FunctionPass(ID) {}

    bool runOnFunction(Function &F) override {
        if (F.hasFnAttribute(Attribute::OptimizeNone)) {
            F.removeFnAttr(Attribute::OptimizeNone);
        }
        return true;
    }
};

char EnableFunctionOptPass::ID = 0;

struct FuncPtrPass : public ModulePass {
    static char ID; // Pass identification, replacement for typeid

    map<Function *, vector<CallBase *>> callGraphNode;
    map<CallBase *, vector<Function *>> callGraphEdge;

    vector<Value *> funcPtr;
    vector<CallBase *> funcCall;
    map<Value *, vector<Value *>> funcPtrBind;
    map<Value *, vector<Function *>> funcPtrValue;

    map<Function *, Value *> funcRetValue;


    FuncPtrPass() : ModulePass(ID) {}

    bool analyseFunction(Function *func, vector<Function *> &reachedFunc) {
        bool changed = false;
        bool tmp;

        assert(func != nullptr);
#ifdef ASSIGNMENT_DEBUG_DUMP
        fprintf(stderr, "[*] Analysing function %s at %p.\n",
                func->getName().data(), func);
#endif

        for (auto &BB: *func) {
            for (auto &inst: BB) {
                if (auto *callBase = dyn_cast<CallBase>(&inst)) { // Handle Callsite
                    vector<Function *> calledFuncs;
                    if (!callBase->isIndirectCall()) { // Handle direct call
                        auto *calledFunc = callBase->getCalledFunction();
                        auto calledFuncName = calledFunc->getName();
                        if (!calledFuncName.startswith("llvm.dbg")) { // Disregard llvm internal debug functions
                            tmp = add_if_not_exist(callGraphNode[func], callBase);
                            tmp |= add_if_not_exist(callGraphEdge[callBase], calledFunc);
                            tmp |= add_if_not_exist(reachedFunc, calledFunc);
                            changed |= tmp;
                            calledFuncs.push_back(calledFunc);
#ifdef ASSIGNMENT_DEBUG_DUMP
                            fprintf(stderr, "\t- Handling direct function call %s (%p) at line %d: %d.\n",
                                    calledFuncName.data(), calledFunc, inst.getDebugLoc().getLine(), tmp);
#endif
                        }

                    } else { // Handle indirect call
                        Value *calledFuncValue = callBase->getCalledOperand(); // This should be a function pointer definition statement

                        tmp = add_if_not_exist(funcPtr, calledFuncValue);
                        tmp |= add_if_not_exist(funcCall, callBase);
                        changed |= tmp;

                        copy(funcPtrValue[calledFuncValue].begin(), funcPtrValue[calledFuncValue].end(),
                             back_inserter(calledFuncs));
                        for (auto bindedFuncPtr: funcPtrBind[calledFuncValue]) {
                            copy(funcPtrValue[bindedFuncPtr].begin(), funcPtrValue[bindedFuncPtr].end(),
                                 back_inserter(calledFuncs));
                        }


#ifdef ASSIGNMENT_DEBUG_DUMP
                        fprintf(stderr, "\t- Handling indirect function (pointer) call %s (%p) at line %d: %d.\n",
                                calledFuncValue->getName().data(), calledFuncValue,
                                inst.getDebugLoc().getLine(), tmp);
#endif
                    }

                    for (auto calledFunc: calledFuncs) {
#ifdef ASSIGNMENT_DEBUG_DUMP
                        fprintf(stderr, "\t\t- Possible callee for this callsite: %s (%p).\n",
                                calledFunc->getName().data(), calledFunc);
#endif
                        // Handle call function pointer argument to parameter binding
                        for (auto &callArgument: callBase->args()) {
                            // Process function pointers only
                            if (!callArgument->getType()->isPointerTy()) continue;
                            if (!dyn_cast<PointerType>(
                                    callArgument->getType())->getElementType()->isFunctionTy())
                                continue;

                            auto callParameter = calledFunc->getArg(callArgument.getOperandNo());

                            tmp = add_if_not_exist(funcPtr, cast<Value>(callParameter));
                            if (!isa<Function>(callArgument)) { // Assign a function pointer argument to parameter
                                tmp |= add_if_not_exist(funcPtr, cast<Value>(callArgument));
                                tmp |= add_if_not_exist(funcPtrBind[cast<Value>(callParameter)],
                                                        cast<Value>(callArgument));

#ifdef ASSIGNMENT_DEBUG_DUMP
                                fprintf(stderr,
                                        "\t\t\t- Handling %dth function pointer argument %s (%p) to parameter %s (%p) binding: %d.\n",
                                        callArgument.getOperandNo(), callArgument->getName().data(), &callArgument,
                                        callParameter->getName().data(), callParameter, tmp);
#endif
                            } else { // Assign a function entity argument to parameter
                                tmp |= add_if_not_exist(funcPtrValue[cast<Value>(callParameter)],
                                                        cast<Function>(callArgument));
#ifdef ASSIGNMENT_DEBUG_DUMP
                                fprintf(stderr,
                                        "\t\t\t- Handling %dth function entity argument %s (%p) to parameter %s (%p) binding: %d.\n",
                                        callArgument.getOperandNo(), callArgument->getName().data(), &callArgument,
                                        callParameter->getName().data(), callParameter, tmp);
#endif
                            }
                            changed |= tmp;
                        }

                        // Handle call function pointer return value binding
                        do {
                            auto functionRetValType = calledFunc->getReturnType();
                            if (!functionRetValType->isPointerTy()) break;
                            if (!dyn_cast<PointerType>(functionRetValType)->getElementType()->isFunctionTy())
                                break;
                            if (!funcRetValue.count(calledFunc)) {
                                // calledFunc hasn't been visited, defering process
#ifdef ASSIGNMENT_DEBUG_DUMP
                                fprintf(stderr, "\t\t\t- Defered handling function pointer return value binding.\n");
#endif
                                changed = true;
                            } else {
                                // calledFunc has been visited, do binding
                                tmp = add_if_not_exist(funcPtrBind[cast<Value>(callBase)],
                                                       funcRetValue[calledFunc]);
                                changed |= tmp;
#ifdef ASSIGNMENT_DEBUG_DUMP
                                fprintf(stderr, "\t\t\t- Handling function pointer return value binding from caller site: "
                                                "%s -> %s: %d\n", funcRetValue[calledFunc]->getName().data(),
                                        callBase->getName().data(), tmp);
#endif
                            }
                        } while (false);
                    }

                } else if (auto *phiNode = dyn_cast<PHINode>(&inst)) { // Handle PhiNode
                    // Process function pointers only
                    if (!phiNode->getType()->isPointerTy()) continue;
                    if (!dyn_cast<PointerType>(phiNode->getType())->getElementType()->isFunctionTy()) continue;

                    for (auto &use: phiNode->operands()) {
                        if (Function *calleeFunc = dyn_cast<Function>(&use)) {
                            tmp = add_if_not_exist(funcPtrValue[cast<Value>(phiNode)], calleeFunc);
                            changed |= tmp;
#ifdef ASSIGNMENT_DEBUG_DUMP
                            fprintf(stderr, "\t- Possible callee for %s: %s, %d.\n",
                                    phiNode->getName().data(), calleeFunc->getName().data(), tmp);
#endif
                        } else if (ConstantPointerNull *nullFuncPtr = dyn_cast<ConstantPointerNull>(&use)) {
#ifdef ASSIGNMENT_DEBUG_DUMP
                            fprintf(stderr, "\t- Possible callee for %s: NULL, discarded.\n",
                                    phiNode->getName().data());
#endif
                        } else if (use->getType()->isPointerTy() &&
                                   dyn_cast<PointerType>(use->getType())->getElementType()->isFunctionTy()) {
                            tmp = add_if_not_exist(funcPtr, cast<Value>(phiNode));
                            tmp |= add_if_not_exist(funcPtr, cast<Value>(&use));
                            tmp |= add_if_not_exist(funcPtrBind[cast<Value>(phiNode)], cast<Value>(&use));
                            changed |= tmp;
#ifdef ASSIGNMENT_DEBUG_DUMP
                            fprintf(stderr, "\t- Binding function pointer %s to %s: %d.\n",
                                    use->getName().data(), phiNode->getName().data(), tmp);
#endif
                        } else {
                            assert(false);
                        }
                    }
                } else if (auto * retInst = dyn_cast<ReturnInst>(&inst)) {
                    tmp = funcRetValue.count(func);
                    if (!tmp) funcRetValue[func] = retInst->getReturnValue();
#ifdef ASSIGNMENT_DEBUG_DUMP
                    fprintf(stderr, "\t- Handling function pointer return value binding from callee site: "
                                    "%s: %d.\n", retInst->getReturnValue()->getName().data(), !tmp);
#endif
                }
            }
        }
        return changed;
    }

    void buildCallGraph(Module &m, Function *entrypoint) {
        vector<Function *> reachedFunc;
        bool callGraphChanged = true;
        bool tmp;

        callGraphNode[entrypoint].push_back(nullptr);
        reachedFunc.push_back(entrypoint);

#ifdef ASSIGNMENT_DEBUG_DUMP
        int loopCounter = 0;
#endif

        while (callGraphChanged) {
            callGraphChanged = false;
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr, "[*] Building call graph: loop %d.\n", ++loopCounter);
#endif
            for (auto *reachedFuncPtr: reachedFunc) {
                callGraphChanged |= analyseFunction(reachedFuncPtr, reachedFunc);
            }

            bool funcPtrChanged = true;
            while (funcPtrChanged) {
                funcPtrChanged = false;
#ifdef ASSIGNMENT_DEBUG_DUMP
                fprintf(stderr, "[*] Building call graph: propagating bindings: loop %d.\n", loopCounter);
#endif
                for (auto funcPtr1: funcPtr) {
                    for (auto funcPtr2: funcPtr) {
                        if (funcPtr1 == funcPtr2) continue;
                        auto &funcPtr1Bind = funcPtrBind[funcPtr1], &funcPtr2Bind = funcPtrBind[funcPtr2];
                        if (count(funcPtr1Bind.begin(), funcPtr1Bind.end(), funcPtr2)) {
                            // Sort two funcPtrBind vector to compare them in order to decide whether to propagate bindings
                            stable_sort(funcPtr1Bind.begin(), funcPtr1Bind.end());
                            stable_sort(funcPtr2Bind.begin(), funcPtr2Bind.end());
                            if (!includes(funcPtr1Bind.begin(), funcPtr1Bind.end(),
                                          funcPtr2Bind.begin(), funcPtr2Bind.end())) {
                                vector<Value *> tmpContainer;
#ifdef ASSIGNMENT_DEBUG_DUMP
                                fprintf(stderr, "\t- Transmitting function pointer closure "
                                                "%s (%p) to %s (%p).\n",
                                        funcPtr2->getName().data(), funcPtr2,
                                        funcPtr1->getName().data(), funcPtr1);
#endif
                                // Union funcPtr2Bind to funcPtr1Bind if they are not equal
                                set_union(funcPtr1Bind.begin(), funcPtr1Bind.end(),
                                          funcPtr2Bind.begin(), funcPtr2Bind.end(),
                                          back_inserter(tmpContainer));
                                // Swap the content of tmpContainer (unioned result) and funcPtr1Bind which reduced
                                // the copy overhead and release the useless data chunk after leaving the scope
                                funcPtr1Bind.swap(tmpContainer);
                                funcPtrChanged = true;
                            }
                        }
                    }
                }
#ifdef ASSIGNMENT_DEBUG_DUMP
                fprintf(stderr, "[*] Building call graph: modifying call graph: loop %d.\n", loopCounter);
#endif
                for (auto callBase: funcCall) {
                    auto calledFuncPtr = callBase->getCalledOperand();

                    for (auto maybeCalleeFunc: funcPtrValue[calledFuncPtr]) {
                        tmp = add_if_not_exist(callGraphNode[maybeCalleeFunc], static_cast<CallBase *>(nullptr));
                        tmp |= add_if_not_exist(callGraphEdge[callBase], maybeCalleeFunc);
                        tmp |= add_if_not_exist(reachedFunc, maybeCalleeFunc);
#ifdef ASSIGNMENT_DEBUG_DUMP
                        fprintf(stderr, "\t- Adding new function %s in %s through PVVal, %d.\n",
                                maybeCalleeFunc->getName().data(), calledFuncPtr->getName().data(), tmp);
#endif
                    }

                    for (auto bindingFuncPtr: funcPtrBind[calledFuncPtr]) {
#ifdef ASSIGNMENT_DEBUG_DUMP
                        fprintf(stderr, "\t- Adding functions via %s through PVBind.\n",
                                calledFuncPtr->getName().data());
#endif
                        for (auto maybeCalleeFunc: funcPtrValue[bindingFuncPtr]) {
                            tmp = add_if_not_exist(callGraphNode[maybeCalleeFunc], static_cast<CallBase *>(nullptr));
                            tmp |= add_if_not_exist(callGraphEdge[callBase], maybeCalleeFunc);
                            tmp |= add_if_not_exist(reachedFunc, maybeCalleeFunc);
#ifdef ASSIGNMENT_DEBUG_DUMP
                            fprintf(stderr, "\t\t- Adding new function %s in %s through PVBind, %d.\n",
                                    maybeCalleeFunc->getName().data(), bindingFuncPtr->getName().data(), tmp);
#endif
                        }
                    }
                }
            }
        }

    }

    void printResult() {
        map<unsigned int, vector<string>> sortContainer;

        for_each(callGraphEdge.begin(), callGraphEdge.end(), [&](auto &pair) {
            CallBase *callBase = pair.first;
            vector<Function *> callees = pair.second;
            unsigned int sourceLine = callBase->getDebugLoc().getLine();
            for_each(callees.begin(), callees.end(), [&](Function *callee) {
                sortContainer[sourceLine].push_back(callee->getName().data());
            });
        });

        for_each(sortContainer.begin(), sortContainer.end(), [&](auto &lineCalleeNamePairs) {
            int lineNumber = lineCalleeNamePairs.first;
            vector<string> &lineCallees = lineCalleeNamePairs.second;
            stable_sort(lineCallees.begin(), lineCallees.end());
            printf("%u :", lineNumber);
            bool flag = true;
            for (auto &maybeCalledFuncName: lineCallees) {
                printf(", %s" + flag, maybeCalledFuncName.data());
                flag = false;
            }
            printf("\n");
        });
    }

    void main(Module &m) {
        for (auto &funcIter: m) {
            if (!funcIter.getName().startswith("llvm.dbg")) {
                buildCallGraph(m, &funcIter);
            }
        }
        printResult();
    }

    bool runOnModule(Module &M) override {
        //errs() << "Hello: ";
        //errs().write_escaped(M.getName()) << '\n';
        //M.dump();
        //errs() << "------------------------------\n";
        main(M);
        return false;
    }
};

char FuncPtrPass::ID = 0;
static RegisterPass<FuncPtrPass> X("funcptrpass", "Print function call instruction");

static cl::opt<std::string>
        InputFilename(cl::Positional,
                      cl::desc("<filename>.bc"),
                      cl::init(""));


int main(int argc, char **argv) {
    LLVMContext &Context = getGlobalContext();
    SMDiagnostic Err;
    // Parse the command line to read the Inputfilename
    cl::ParseCommandLineOptions(argc, argv,
                                "FuncPtrPass \n Analyse function invocations.\n");


    // Load the input module
    std::unique_ptr<Module> M = parseIRFile(InputFilename, Err, Context);
    if (!M) {
        Err.print(argv[0], errs());
        return 1;
    }

    llvm::legacy::PassManager Passes;

    ///Remove functions' optnone attribute in LLVM5.0
    Passes.add(new EnableFunctionOptPass());
    ///Transform it to SSA
    Passes.add(llvm::createPromoteMemoryToRegisterPass());

    /// Your pass to print Function and Call Instructions
    Passes.add(new FuncPtrPass());
    Passes.run(*M.get());
}

