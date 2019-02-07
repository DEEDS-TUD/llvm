#include "llvm/Transforms/InfluenceTracing/InfluenceTracing.h"
#include "llvm/Pass.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Support/raw_ostream.h"
#include <vector>

using namespace llvm;

DenseMap<unsigned, DebugLoc> inputInstructionLocations = DenseMap<unsigned, DebugLoc>();

PreservedAnalyses InfluenceTracingTagPass::run(Module &M, ModuleAnalysisManager &AM) {
  for (Function& F: M) {
    for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
      DebugLoc DL = I->getDebugLoc();
      if (!DL || isa<DbgInfoIntrinsic>(*I) ||
          (cast<DIScope>(DL.getScope()))->getFilename() != M.getSourceFileName())
        continue;

      bool linePresent = false;
      for (uint64_t i = 1; i < tagwatermark; i++) {
        if (DL.getLine() == inputInstructionLocations[i].getLine()) { //TODO: different files
          linePresent = true;
          std::set<unsigned> influencer;
          influencer.insert(i);
          addInfluencers(&(*I), influencer);
          break;
        }
      }
      if (!linePresent) {
        std::set<unsigned> influencer;
        influencer.insert(tagwatermark);
        addInfluencers(&(*I), influencer);
        inputInstructionLocations[tagwatermark] = DL;
        tagwatermark++;
      }
    }
  }
  return PreservedAnalyses::all();
}

PreservedAnalyses InfluenceTracingDetectEliminatedCodePass::run(Module &M, ModuleAnalysisManager &AM) {
  DenseMap<unsigned, DebugLoc> eliminatedInstructions(inputInstructionLocations);
  for (Function& F: M) {
    for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
      LLVMContext& C = I->getContext();
      std::set<unsigned> influencers = getInfluencersRecursive(*I);
      I->setMetadata("inftrc", getInfluenceTracesTuple(C, influencers));
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
  }
  if (eliminatedInstructions.size()) {
    //errs() << eliminatedInstructions.size() << "/" << inputCnt << " instructions were eliminated:\n";
    for (auto i: eliminatedInstructions) {
       M.getContext().diagnose(DiagnosticInfoEliminatedCode(*M.begin(), DiagnosticLocation(i.second), i.first));
    }
  }
  return PreservedAnalyses::all();
}

namespace {
  uint64_t tagwatermark = 1;
  
  void addInfluencersAsMetadata(Instruction& I) {
    LLVMContext& C = I.getContext();
    std::set<unsigned> influencers = getInfluencersRecursive(I);
    I.setMetadata("inftrc", getInfluenceTracesTuple(C, influencers));
  }

  void tagInstruction(Instruction& I, uint64_t tag) {
    std::set<unsigned> influencer;
    influencer.insert(tag);
    addInfluencers(&I, influencer);
  }
  
  struct InfluenceTracingTag : FunctionPass {
    static char ID;
    InfluenceTracingTag() : FunctionPass(ID) {}

    bool runOnFunction(Function &F) override {
      for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
        DebugLoc DL = I->getDebugLoc();
        if (!DL || isa<DbgInfoIntrinsic>(*I) ||
            (cast<DIScope>(DL.getScope()))->getFilename() != F.getParent()->getSourceFileName())
          continue;

        bool linePresent = false;
        /*for (uint64_t i = 1; i < tagwatermark; i++) {
          if (DL.getLine() == inputInstructionLocations[i].getLine()) {
            linePresent = true;
            std::set<unsigned> influencer;
            influencer.insert(i);
            addInfluencers(&(*I), influencer);
            break;
          }
        }*/
        if (!linePresent) {
          std::set<unsigned> influencer;
          influencer.insert(tagwatermark);
          addInfluencers(&(*I), influencer);
          inputInstructionLocations[tagwatermark] = DL;
          tagwatermark++;
        }
      }
      return false;
    }

    // We don't modify the program, so we preserve all analyses.
    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesAll();
    }
  };

  struct InfluenceTracingDetectEliminatedInstructions : ModulePass {
    static char ID;
    InfluenceTracingDetectEliminatedInstructions() : ModulePass(ID) {}
    
    bool runOnModule(Module &M) override {
      DenseMap<unsigned, DebugLoc> eliminatedInstructions(inputInstructionLocations);
      unsigned inputCnt = inputInstructionLocations.size();
      for (Function& F: M) {
        for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
          //LLVMContext& C = I->getContext();
          std::set<unsigned> influencers = getInfluencersRecursive(*I);
          for (auto inf: influencers) {
            eliminatedInstructions.erase(inf);
          }
          I->setMetadata("inftrc", getInfluenceTracesTuple(I->getContext(), influencers));
          /*MDTuple* IT = dyn_cast_or_null<MDTuple>(I->getMetadata("inftrc"));
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
          }*/
        }
      }
      if (eliminatedInstructions.size()) {
        errs() << eliminatedInstructions.size() << "/" << inputCnt << " instructions were eliminated:\n";
        for (auto i: eliminatedInstructions) {
           M.getContext().diagnose(DiagnosticInfoEliminatedCode(*M.begin(), DiagnosticLocation(i.second), i.first));
        }
      }
      return false;
    }

    // We don't modify the program, so we preserve all analyses.
    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesAll();
    }
  };
}

char InfluenceTracingTag::ID = 0;

INITIALIZE_PASS_BEGIN(InfluenceTracingTag, "ittag",
    "Influence Tracing tagging pass", false, false)
INITIALIZE_PASS_END(InfluenceTracingTag, "ittag",
    "Influence Tracing tagging pass", false, false)

char InfluenceTracingDetectEliminatedInstructions::ID = 0;
INITIALIZE_PASS_BEGIN(InfluenceTracingDetectEliminatedInstructions, "itdei",
    "Influence Tracing detect eliminated instructions pass", false, false)
INITIALIZE_PASS_END(InfluenceTracingDetectEliminatedInstructions, "itdei",
    "Influence Tracing detect eliminated instructions pass", false, false)

// Initialization Routines
void llvm::initializeInfluenceTracing(PassRegistry &Registry) {
  initializeInfluenceTracingTagPass(Registry);
  initializeInfluenceTracingDetectEliminatedInstructionsPass(Registry);
}

void LLVMInitializeInfluenceTracing(LLVMPassRegistryRef R) {
  initializeInfluenceTracing(*unwrap(R));
}

FunctionPass *llvm::createInfluenceTracingTagPass() {
  return new InfluenceTracingTag();
}

ModulePass *llvm::createInfluenceTracingDetectEliminatedCodePass() {
  return new InfluenceTracingDetectEliminatedInstructions();
}
