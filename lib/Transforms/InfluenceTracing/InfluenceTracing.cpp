//#include "llvm/Transforms/InfluenceTracing/InfluenceTracing.h"
#include "llvm/Pass.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Support/raw_ostream.h"
#include <vector>

using namespace llvm;

namespace {
  uint64_t tagwatermark = 1;
  
  DenseMap<unsigned, DebugLoc> inputInstructionLocations = DenseMap<unsigned, DebugLoc>();
  
  void tagInstruction(Instruction& I, uint64_t tag) {
    LLVMContext& C = I.getContext();
    ConstantInt* CI = ConstantInt::get(IntegerType::get(C, 64), tag);
    ConstantAsMetadata* CAM = ConstantAsMetadata::get(CI);
    MDNode* MD = MDNode::get(C, CAM);
    I.setMetadata("inftrc", MD);
  }
  
  struct InfluenceTracingTag : FunctionPass {
    static char ID;
    InfluenceTracingTag() : FunctionPass(ID) {}
    
    bool runOnFunction(Function &F) override {
      for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
        DebugLoc DL = I->getDebugLoc();
        if (!DL || isa<DbgInfoIntrinsic>(*I))
          continue;
        tagInstruction(*I, tagwatermark);
        inputInstructionLocations[tagwatermark] = DL;
        tagwatermark++;
      }
      return false;
    }
  };
  
  struct InfluenceTracingDetectEliminatedInstructions : FunctionPass {
    static char ID;
    InfluenceTracingDetectEliminatedInstructions() : FunctionPass(ID) {}
    
    bool runOnFunction(Function &F) override {
      DenseMap<unsigned, DebugLoc> eliminatedInstructions(inputInstructionLocations);
      unsigned inputCnt = inputInstructionLocations.size();
      for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
        MDTuple* IT = dyn_cast_or_null<MDTuple>(I->getMetadata("inftrc"));
        if(IT) {
          for (auto i = IT->op_begin(); i != IT->op_end(); i++) {
            const ConstantAsMetadata* CAM = dyn_cast<const ConstantAsMetadata>(i->get());
            if (CAM) {
              ConstantInt* tag = dyn_cast<ConstantInt>(CAM->getValue());
              if (tag) {
                eliminatedInstructions.erase(tag->getLimitedValue());
              }
            }
          }
        }
      }
      errs() << eliminatedInstructions.size() << "/" << inputCnt << " instructions were eliminated:\n";
      for (auto i: eliminatedInstructions) {
        errs() << "\t";
        i.second.print(errs());
        errs() << "[" << i.first << "]\n";
      }
      return false;
    }
  };
}
  
char InfluenceTracingTag::ID = 0;
static RegisterPass<InfluenceTracingTag> X("ittag", "Influence Tracing tagging pass",
                                            false,
                                            false);
  
char InfluenceTracingDetectEliminatedInstructions::ID = 0;
static RegisterPass<InfluenceTracingDetectEliminatedInstructions> Y("itdei", "Influence Tracing detect eliminated instructions pass",
                                            false,
                                            false);
