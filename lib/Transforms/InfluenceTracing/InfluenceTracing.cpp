#include "llvm/Transforms/InfluenceTracing/InfluenceTracing.h"
#include "llvm/Pass.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Support/raw_ostream.h"
#include <vector>

using namespace llvm;

DenseMap<trace_t, DebugLoc> inputInstructionLocations = DenseMap<trace_t, DebugLoc>();

/*
 * Converts a set of ints to a metadata tuple.
 */
static MDTuple* getInfluenceTracesTuple(LLVMContext& C, traces_t influencers) {
  std::vector<Metadata*> mdTags;
  for(trace_t i: influencers) {
    ConstantInt* CI = ConstantInt::get(IntegerType::get(C, sizeof(trace_t) * 8), i);
    ConstantAsMetadata* CAM = ConstantAsMetadata::get(CI);
    mdTags.push_back(CAM);
  }
  return MDTuple::get(C, ArrayRef<Metadata*>(mdTags));
}

static traces_t getInfluencersAndOperandInfluencers(const Instruction& I) {
  traces_t influencers = I.getInfluenceTraces();
  for (Value* O: I.operands()) {
    if (!dyn_cast<Instruction>(O)) {
      join(influencers, O->getInfluenceTraces());
    }
  }
  return influencers;
}

PreservedAnalyses InfluenceTracingTagPass::run(Module &M, ModuleAnalysisManager &AM) {
  for (Function& F: M) {
    for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
      DebugLoc DL = I->getDebugLoc();
      IntrinsicInst* II;
      if (
         // Do not track debug intrinsics
         !DL || isa<DbgInfoIntrinsic>(*I) ||
         // nor lifetime intrinsics (or instructions that are solely created for lifetime intrinsic uses)
         ((II = dyn_cast<IntrinsicInst>(&(*I))) && (II->getIntrinsicID() == Intrinsic::lifetime_start || II->getIntrinsicID() == Intrinsic::lifetime_end)) ||
         (!I->user_empty() && onlyUsedByLifetimeMarkers(&(*I))) ||
         // and only instructions originating in the main module (not from included files)
         (cast<DIScope>(DL.getScope()))->getFilename() != F.getParent()->getSourceFileName() ||
         // and no instruction on line 0
         DL->getLine() == 0)
       continue;

      bool linePresent = false;
      for (uint64_t i = 1; i < tagwatermark; i++) {
        if (DL.getLine() == inputInstructionLocations[i].getLine()) { //TODO: different files
          linePresent = true;
          traces_t influencer;
          influencer.insert(i);
          (*I).addInfluencers(influencer);
          break;
        }
      }
      if (!linePresent) {
        traces_t influencer;
        influencer.insert(tagwatermark);
        (*I).addInfluencers(influencer);
        inputInstructionLocations[tagwatermark] = DL;
        tagwatermark++;
      }
    }
  }
  return PreservedAnalyses::all();
}

PreservedAnalyses InfluenceTracingDetectEliminatedCodePass::run(Module &M, ModuleAnalysisManager &AM) {
  DenseMap<trace_t, DebugLoc> eliminatedInstructions(inputInstructionLocations);
  for (Function& F: M) {
    for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
      LLVMContext& C = I->getContext();
      traces_t influencers = getInfluencersAndOperandInfluencers(*I);
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
    traces_t influencers = getInfluencersAndOperandInfluencers(I);
    I.setMetadata("inftrc", getInfluenceTracesTuple(C, influencers));
  }

  void tagInstruction(Instruction& I, uint64_t tag) {
    traces_t influencer;
    influencer.insert(tag);
    I.addInfluencers(influencer);
  }
  
  struct InfluenceTracingTag : FunctionPass {
    static char ID;
    InfluenceTracingTag() : FunctionPass(ID) {}

    bool runOnFunction(Function &F) override {
      for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
        DebugLoc DL = I->getDebugLoc();
        IntrinsicInst* II;
        if (
            // Do not track debug intrinsics
            !DL || isa<DbgInfoIntrinsic>(*I) ||
            // nor invariant or lifetime intrinsics (or instructions that are solely created for lifetime intrinsic uses)
            ((II = dyn_cast<IntrinsicInst>(&(*I))) &&
                (II->getIntrinsicID() == Intrinsic::lifetime_start ||
                 II->getIntrinsicID() == Intrinsic::lifetime_end ||
                 II->getIntrinsicID() == Intrinsic::invariant_start ||
                 II->getIntrinsicID() == Intrinsic::invariant_end)) ||
            (!I->user_empty() && onlyUsedByLifetimeMarkers(&(*I))) ||
            // and only instructions originating in the main module (not from included files)
            (cast<DIScope>(DL.getScope()))->getFilename() != F.getParent()->getSourceFileName() ||
            // and no instruction on line 0 or above 5000
            DL->getLine() == 0 || DL->getLine() > 5000)
          continue;

        if (DL.getLine() == 56) {
          //I->print(errs());
          //errs() << "\n";
        }

        bool linePresent = false;
        for (uint64_t i = 1; i < tagwatermark; i++) {
          if (DL.getLine() == inputInstructionLocations[i].getLine()) {
            linePresent = true;
            traces_t influencer;
            influencer.insert(i);
            (*I).addInfluencers(influencer);
            break;
          }
        }
        if (!linePresent) {
          traces_t influencer;
          influencer.insert(tagwatermark);
          (*I).addInfluencers(influencer);
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
      DenseMap<trace_t, DebugLoc> eliminatedInstructions(inputInstructionLocations);
      unsigned inputCnt = inputInstructionLocations.size();
      for (Function& F: M) {
        for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
          //LLVMContext& C = I->getContext();
          traces_t influencers = getInfluencersAndOperandInfluencers(*I);
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
      for (GlobalValue& GV: M.globals()) {
        traces_t influencers = GV.getInfluenceTraces();
        for (auto inf: influencers) {
          eliminatedInstructions.erase(inf);
        }
      }
      if (eliminatedInstructions.size()) {
        for (auto i: eliminatedInstructions) {
           M.getContext().diagnose(DiagnosticInfoEliminatedCode(*M.begin(), DiagnosticLocation(i.second), i.first));
        }
        errs() << eliminatedInstructions.size() << "/" << inputCnt << " instructions were eliminated:\n";
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
