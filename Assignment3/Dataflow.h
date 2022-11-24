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

using namespace llvm;

///Base dataflow visitor class, defines the dataflow function

template<class T>
class DataflowVisitor {
public:
    virtual ~DataflowVisitor() {}

    /// Dataflow Function invoked for each basic block 
    /// 
    /// @block the Basic Block
    /// @DFVal the input dataflow value
    /// @isForward true to compute inputDFVal forward, otherwise backward
    virtual void transferBasicBlock(BasicBlock *block, T *inputDFVal, bool isForward) {
        if (isForward) {
            for (auto ii = block->begin(); ii != block->end(); ++ii) {
                Instruction *inst = &*ii;
                transferInst(inst, inputDFVal);
            }
        } else {
            for (auto rii = block->rbegin(); rii != block->rend(); ++rii) {
                Instruction *inst = &*rii;
                transferInst(inst, inputDFVal);
            }
        }
    }

    ///
    /// Dataflow Function invoked for each instruction
    ///
    /// @inst the Instruction
    /// @dfval the input dataflow value
    /// @return true if dfval changed
    virtual void transferInst(Instruction *inst, T *inputDFVal) = 0;

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
    bool operator == (const DataflowFactPair<T> &fp2) const {
        return this->input == fp2.input && this->output == fp2.output;
    }
};

template<class T>
struct DataflowResult {
    typedef typename std::map<BasicBlock *, DataflowFactPair<T> > Type;
};

/// 
/// Compute a forward iterated fixedpoint dataflow function, using a user-supplied
/// visitor function. Note that the caller must ensure that the function is
/// in fact a monotone function, as otherwise the fixedpoint may not terminate.
/// 
/// @param fn The function
/// @param visitor A function to compute dataflow vals
/// @param result The results of the dataflow 
/// @initval the Initial dataflow value
template<class T>
void analyzeForward(Function *fn,
                    DataflowVisitor<T> *visitor,
                    typename DataflowResult<T>::Type *result,
                    const T &initVal) {
    return;
}

///
/// Compute a backward iterated fixedpoint dataflow function, using a user-supplied
/// visitor function. Note that the caller must ensure that the function is
/// in fact a monotone function, as otherwise the fixedpoint may not terminate.
/// 
/// @param fn The function
/// @param visitor A function to compute dataflow vals
/// @param result The results of the dataflow 
/// @initval The initial dataflow value
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

        // Merge all incoming value
        T bbFact = result[bb].output; // Warning: assign constructor used here!
        // Since this program point, bbFact is bb's output fact
        for (auto si = succ_begin(bb); si != succ_end(bb); ++si) {
            BasicBlock *succBB = *si;
            visitor->merge(&bbFact, result[succBB].input);
        }
        result[bb].output = bbFact; // Warning: assign constructor used here!

        // Transfer basic block
        visitor->transferBasicBlock(bb, &bbFact, false);
        // From now on, bbFact is bb's input fact

        // If outgoing value changed, propagate it along the CFG
        if (bbFact == result[bb].input) continue;
        result[bb].input = bbFact; // Warning: assign constructor used here!

        // Insert precedent basic block into workList
        for (pred_iterator pi = pred_begin(bb); pi != pred_end(bb); ++pi) {
            workList.insert(*pi);
        }
    }
}

template<class T>
void printDataflowResult(raw_ostream &out,
                         const typename DataflowResult<T>::Type &DFResult) {
    for (typename DataflowResult<T>::Type::const_iterator it = DFResult.begin(); it != DFResult.end(); ++it) {
        if (it->first == NULL) out << "*";
        else it->first->dump();

        out << "\n\tInput Fact: "
            << it->second.input
            << "\n\tOutput Fact: "
            << it->second.output
            << "\n";
    }
}


#endif /* !_DATAFLOW_H_ */
