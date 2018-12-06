
#ifndef LLVM_TRANSFORMS_INFLUENCE_TRACING_H
#define LLVM_TRANSFORMS_INFLUENCE_TRACING_H

#include "llvm/IR/Constants.h"
#include "llvm/IR/Instruction.h"
#include <initializer_list>
#include <set>


#include "llvm/Support/raw_ostream.h"
namespace llvm {
  /*
   * Converts a set of ints to a metadata tuple.
   */
  static MDTuple* getInfluenceTracesTuple(LLVMContext& C, std::set<unsigned> influencers) {
	/*for(unsigned i: influencers) {
		errs() << i << ", ";
	}
	errs() << "\n";*/
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
  static std::set<unsigned> getInfluencers(Instruction& I) {
    std::set<unsigned> influencers;
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
    }
    return influencers;
  }
  
  /*
   * Returns a set of all influencers of this Instruction and its operands (recursive).
   */
  //TODO: naming
  static std::set<unsigned> getInfluencersRecursive(Instruction& I) {
    std::set<unsigned> influencers = getInfluencers(I);
    unsigned numOperands = I.getNumOperands();
    for (unsigned i = 0; i < numOperands; i++) {
      Instruction* O = dyn_cast_or_null<Instruction>(I.getOperand(i));
      if (O) {
        std::set<unsigned> influencers2 = getInfluencers(*O);
        join(influencers, influencers2);
      }
    }
    return influencers;
  }
  
  /*
   * Marks I as influenced by all int in the set.
   */
  static void setInfluenceTraces(Instruction& I, std::set<unsigned> influencers) {
    LLVMContext& C = I.getContext();
    I.setMetadata("inftrc", getInfluenceTracesTuple(C, influencers));
  }
  
  /*
   * Joins the influencers with the existing influencers of I.
   */
  static void addInfluencers(Instruction& I, std::set<unsigned>& influencers) {
    std::set<unsigned> influencersI = getInfluencers(I);
    join(influencersI, influencers);
    setInfluenceTraces(I, influencersI);
  }
  
  /*
   * Joins the influencers of B with the existing influencers for A.
   */
  static void addInfluencers(Instruction& A, Instruction& B) {
    std::set<unsigned> influencersB = getInfluencers(B);
    addInfluencers(A, influencersB);
  }
  
  /*
   * Marks V, or all Instructions using V if V is not an instruction, as influenced by I.
   */
  static void propagateInfluenceTraces(Value* V, Instruction& I) {
    if(V == nullptr) {
      errs() << "Null propagate\n";
      return;
    }
    std::set<unsigned> influencers = getInfluencersRecursive(I);
    if (Instruction* I2 = dyn_cast<Instruction>(V)) {
      addInfluencers(*I2, influencers);
    }
    else {
      for (User* U: I.users()) {
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
    }
  }
}

#endif
