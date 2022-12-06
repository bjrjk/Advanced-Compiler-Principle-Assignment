/************************************************************************
 *
 * @file Dataflow.h
 *
 * General dataflow framework
 *
 ***********************************************************************/

#ifndef _DATAFLOW_H_
#define _DATAFLOW_H_

#include <utility>
#include <set>
#include <map>

#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/Function.h>

#include "debug.h"

using namespace llvm;

template<class T>
struct InterAnalysisInfo;

///Base dataflow visitor class, defines the dataflow function

template<class T>
class DataflowVisitor {
public:
    virtual ~DataflowVisitor() {}

    /// Dataflow Function invoked for each basic block 
    /// 
    /// @block the Basic Block
    /// @DFVal the input dataflow value
    /// @isForward true to compute fact forward, otherwise backward
    virtual void
    transferBasicBlock(BasicBlock *block, T *fact, bool isForward, InterAnalysisInfo<T> &interAnalysisInfo) {
#ifdef ASSIGNMENT_DEBUG_DUMP
        fprintf(stderr, "\t[+] Analyzing Basic Block %p, IR:\n", block);
        stderrCyanBackground();
        block->dump();
        stderrNormalBackground();
#endif
        if (isForward) {
            for (auto ii = block->begin(); ii != block->end(); ++ii) {
                Instruction *inst = &*ii;
                transferInst(inst, fact, interAnalysisInfo);
            }
        } else {
            for (auto rii = block->rbegin(); rii != block->rend(); ++rii) {
                Instruction *inst = &*rii;
                transferInst(inst, fact, interAnalysisInfo);
            }
        }
    }

    ///
    /// Dataflow Function invoked for each instruction
    ///
    /// @inst the Instruction
    /// @dfval the input dataflow value
    /// @return true if dfval changed
    virtual void transferInst(Instruction *inst, T *fact, InterAnalysisInfo<T> &interAnalysisInfo) = 0;

    ///
    /// Merge of two DFVals, dest will be the merged result
    /// @return true if dest changed
    ///
    virtual void merge(T *dest, const T &src) = 0;
};

///
/// Dummy class to provide a typedef for the detailed result set
/// For each basic block, we compute its input dataflow val and its output dataflow val
///
template<class T>
struct DataflowFactPair {
    T input;
    T output;

    DataflowFactPair() {}

    DataflowFactPair(const T &input, const T &output) : input(input), output(output) {}

    bool operator==(const DataflowFactPair<T> &fp2) const {
        return this->input == fp2.input && this->output == fp2.output;
    }
};

template<class T>
struct DataflowResult {
    typedef typename std::map<BasicBlock *, DataflowFactPair<T> > Type;
};

template<class T>
struct InterAnalysisInfo {
    bool isEntryPoint;
    DataflowVisitor<T> *visitor;
    typename DataflowResult<T>::Type *resultContainer;
    Function *function;


    InterAnalysisInfo(bool isEntryPoint, DataflowVisitor<T> *visitor,
                      typename DataflowResult<T>::Type *resultContainer, Function *function) :
        isEntryPoint(isEntryPoint), visitor(visitor),
        resultContainer(resultContainer), function(function) {}
};

/// 
/// Compute a forward iterated fixedpoint dataflow function, using a user-supplied
/// visitor function. Note that the caller must ensure that the function is
/// in fact a monotone function, as otherwise the fixedpoint may not terminate.
///
/// Warning: This is a may forward analysis framework.
/// 
/// @param fn The function
/// @param visitor A function to compute dataflow vals
/// @param resultContainer The results of the dataflow
/// @param initVal the Initial dataflow value
template<class T>
void analyzeForward(Function *fn,
                    DataflowVisitor<T> *visitor,
                    typename DataflowResult<T>::Type *resultContainer,
                    const T &initVal, bool isEntrypoint) {
    typename DataflowResult<T>::Type &result = *resultContainer;
    InterAnalysisInfo<T> interAnalysisInfo(isEntrypoint, visitor, resultContainer, fn);
    std::set<BasicBlock *> workList;

    // Initialize the workList with all blocks
    for (auto bi = fn->begin(); bi != fn->end(); ++bi) {
        BasicBlock *bb = &*bi;
        result[bb] = DataflowFactPair<T>(initVal, initVal);
        workList.insert(bb);
    }

    // Iteratively compute the dataflow result
    while (!workList.empty()) {
        BasicBlock *bb = *workList.begin();
        workList.erase(workList.begin());

        // Merge all incoming(input) value
        T bbFact = result[bb].input; // Warning: assign constructor used here!
        // Since this program point, bbFact is bb's input fact
        for (auto pi = pred_begin(bb); pi != pred_end(bb); ++pi) {
            BasicBlock *predBB = *pi;
            visitor->merge(&bbFact, result[predBB].output);
        }
        result[bb].input = bbFact; // Warning: assign constructor used here!

        // Transfer basic block
        visitor->transferBasicBlock(bb, &bbFact, true, interAnalysisInfo);
        // From now on, bbFact is bb's output fact

        // If outgoing value changed, propagate it along the CFG
        if (bbFact == result[bb].output) continue;
        result[bb].output = bbFact; // Warning: assign constructor used here!

        // Insert successor basic block into workList
        for (auto si = succ_begin(bb); si != succ_end(bb); ++si) {
            workList.insert(*si);
        }
    }
}

///
/// Compute a backward iterated fixedpoint dataflow function, using a user-supplied
/// visitor function. Note that the caller must ensure that the function is
/// in fact a monotone function, as otherwise the fixedpoint may not terminate.
///
/// Warning: This is a may backward analysis framework.
///
/// @param fn The function
/// @param visitor A function to compute dataflow vals
/// @param resultContainer The results of the dataflow
/// @param initVal The initial dataflow value
template<class T>
void analyzeBackward(Function *fn,
                     DataflowVisitor<T> *visitor,
                     typename DataflowResult<T>::Type *resultContainer,
                     const T &initVal) {

    typename DataflowResult<T>::Type &result = *resultContainer;
    std::set<BasicBlock *> workList;

    // Initialize the workList with all blocks
    for (auto bi = fn->begin(); bi != fn->end(); ++bi) {
        BasicBlock *bb = &*bi;
        result[bb] = DataflowFactPair<T>(initVal, initVal);
        workList.insert(bb);
    }

    // Iteratively compute the dataflow result
    while (!workList.empty()) {
        BasicBlock *bb = *workList.begin();
        workList.erase(workList.begin());

        // Merge all incoming(output) value
        T bbFact = result[bb].output; // Warning: assign constructor used here!
        // Since this program point, bbFact is bb's output fact
        for (auto si = succ_begin(bb); si != succ_end(bb); ++si) {
            BasicBlock *succBB = *si;
            visitor->merge(&bbFact, result[succBB].input);
        }
        result[bb].output = bbFact; // Warning: assign constructor used here!

        // Transfer basic block
        visitor->transferBasicBlock(bb, &bbFact); //TODO: Won't be fixed
        // From now on, bbFact is bb's input fact

        // If outgoing value changed, propagate it along the CFG
        if (bbFact == result[bb].input) continue;
        result[bb].input = bbFact; // Warning: assign constructor used here!

        // Insert precedent basic block into workList
        for (auto pi = pred_begin(bb); pi != pred_end(bb); ++pi) {
            workList.insert(*pi);
        }
    }
}

template<class T>
void printDataflowResult(raw_ostream &out,
                         const typename DataflowResult<T>::Type &DFResult) {
    char buf[1024]; // Warning: Buffer Overflow

    sprintf(buf, "[*] Dataflow Result Dump for %p:\n", &DFResult);
    out << buf;
    for (typename DataflowResult<T>::Type::const_iterator it = DFResult.begin(); it != DFResult.end(); ++it) {
        sprintf(buf, "\t[*] Basic Block Fact Dump for %p:\n", it->first);
        out << buf;

        stderrCyanBackground();
        if (it->first == NULL) out << "*";
        else it->first->dump();

        stderrRedFontYellowBackground();
        out << "\n\t[*] Input Fact: \n"
            << it->second.input
            << "\n\t[*] Output Fact: \n"
            << it->second.output
            << "\n";
        stderrNormalBackground();
    }
}

template<class T>
void printDataflowFact(raw_ostream &out,
                       const T &factResult) {
    char buf[1024]; // Warning: Buffer Overflow

    sprintf(buf, "[*] Fact Dump for %p (Single):\n", &factResult);
    out << buf;

    stderrRedFontYellowBackground();
    out << factResult << "\n";
    stderrNormalBackground();
}

#endif /* !_DATAFLOW_H_ */
