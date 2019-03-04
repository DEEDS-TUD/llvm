
#ifndef LLVM_TRANSFORMS_INFLUENCE_TRACING_H
#define LLVM_TRANSFORMS_INFLUENCE_TRACING_H

#include "llvm/IR/Constants.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/PassManager.h"
#include <initializer_list>
#include <set>


#include "llvm/Support/raw_ostream.h"
namespace llvm {

  class InfluenceTracingTagPass
      : public PassInfoMixin<InfluenceTracingTagPass> {

  public:
    InfluenceTracingTagPass() : tagwatermark(1) {};
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

  private:
    uint64_t tagwatermark;
  };
  FunctionPass *createInfluenceTracingTagPass();

  class InfluenceTracingDetectEliminatedCodePass
      : public PassInfoMixin<InfluenceTracingDetectEliminatedCodePass> {

  public:
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  };
  ModulePass *createInfluenceTracingDetectEliminatedCodePass();
  
  /*
   * Joins all elements of B into A.
   */
  static void join(traces_t& A, const traces_t& B) {
    A.insert(B.begin(), B.end());
  }
  
  static inline traces_t getInfluencersRecursive(const Value* V) {
    traces_t influencers = V->getInfluenceTraces();
    if (const Instruction* I = dyn_cast<const Instruction>(V)) {
      for (Value* O: I->operands()) {
        traces_t influencersRecursive = getInfluencersRecursive(O);
        join(influencers, influencersRecursive);
      }
    }
    return influencers;
  }
  
  /*
   * Marks V, or all Instructions using V if V is not an instruction, as influenced by I.
   */
  static void propagateInfluenceTraces(Value* V, const Instruction& I) {
    if(V == nullptr) {
      errs() << "Null propagate\n";
      return;
    }
    V->addInfluencersAndOperandInfluencers(&I);
  }
}

#endif
