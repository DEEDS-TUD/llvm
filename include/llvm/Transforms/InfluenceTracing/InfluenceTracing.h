
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
   * Converts a set of ints to a metadata tuple.
   */
  static MDTuple* getInfluenceTracesTuple(LLVMContext& C, std::set<unsigned> influencers) {
    std::vector<Metadata*> mdTags;
    for(unsigned i: influencers) {
      ConstantInt* CI = ConstantInt::get(IntegerType::get(C, 64), i);
      ConstantAsMetadata* CAM = ConstantAsMetadata::get(CI);
      mdTags.push_back(CAM);
    }
    return MDTuple::get(C, ArrayRef<Metadata*>(mdTags));
  }
  
  /*
   * Joins all elements of B into A.
   */
  static void join(std::set<unsigned>& A, const std::set<unsigned>& B) {
    A.insert(B.begin(), B.end());
  }
  
  /*
   * Returns all influencers of I as a set of unsigned ints.
   */
  static std::set<unsigned> getInfluencers(const Value* V) {
    std::set<unsigned> influencers = V->getInfluenceTraces();
    /*std::set<unsigned> influencers;
    MDTuple* IT = dyn_cast_or_null<MDTuple>(I.getMetadata("inftrc"));
    if(IT) {
      for (auto i = IT->op_begin(); i != IT->op_end(); i++) {
        const ConstantAsMetadata* CAM = dyn_cast<const ConstantAsMetadata>(i->get());
        if (CAM) {
          ConstantInt* tag = dyn_cast<ConstantInt>(CAM->getValue());
          if (tag) {
            influencers.insert(tag->getLimitedValue());
          }
        }
      }
    }*/
    return influencers;
  }
  
  /*
   * Returns a set of all influencers of this Instruction and its operands (recursive).
   */
  //TODO: naming
  static std::set<unsigned> getInfluencersRecursive(const Instruction& I) {
    std::set<unsigned> influencers = getInfluencers(&I);
    unsigned numOperands = I.getNumOperands();
    for (unsigned i = 0; i < numOperands; i++) {
      Instruction* O = dyn_cast_or_null<Instruction>(I.getOperand(i));
      if (O) {
        //std::set<unsigned> influencers2 = getInfluencers(O);
        //join(influencers, influencers2);
      }
      else {
        std::set<unsigned> influencers2 = I.getOperand(i)->getInfluenceTraces();
        join(influencers, influencers2);
      }
    }
    return influencers;
  }
  
  /*
   * Marks I as influenced by all int in the set.
   */
  static void setInfluenceTraces(Value* V, std::set<unsigned> influencers) {
    //LLVMContext& C = I.getContext();
    //I.setMetadata("inftrc", getInfluenceTracesTuple(C, influencers));
    V->setInfluenceTraces(influencers);
  }
  
  /*
   * Joins the influencers with the existing influencers of I.
   */
  static void addInfluencers(Value* V, std::set<unsigned>& influencers) {
    std::set<unsigned> influencersV = getInfluencers(V);
    join(influencersV, influencers);
    setInfluenceTraces(V, influencersV);
  }
  
  /*
   * Joins the influencers of B with the existing influencers for A.
   */
  static void addInfluencers(Instruction& A, const Instruction& B) {
    std::set<unsigned> influencersB = getInfluencers(&B);
    addInfluencers(&A, influencersB);
  }
  
  /*
   * Marks V, or all Instructions using V if V is not an instruction, as influenced by I.
   */
  static void propagateInfluenceTraces(Value* V, const Instruction& I) {
    if(V == nullptr) {
      errs() << "Null propagate\n";
      return;
    }
    std::set<unsigned> influencers = getInfluencersRecursive(I);
    addInfluencers(V, influencers);
    /*if (Instruction* I2 = dyn_cast<Instruction>(V)) {
      addInfluencers(*I2, influencers);
    }
    else {
      for (User* U: V->users()) {
        if (Instruction* I2 = dyn_cast<Instruction>(U)) {
          //keep old and new traces
          std::set<unsigned> influencers2 = getInfluencers(*I2);
          join(influencers2, influencers);
          setInfluenceTraces(*I2, influencers2);
        }
        else {
          U->print(errs());
          errs() << "\n";
        }
      }
    }*/
  }
}

#endif
