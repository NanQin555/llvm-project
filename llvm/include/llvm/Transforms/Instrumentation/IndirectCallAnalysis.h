#ifndef LLVM_ANALYSIS_INDIRECTCALLANALYSIS
#define LLVM_ANALYSIS_INDIRECTCALLANALYSIS

#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/IR/PassManager.h"
namespace llvm {

// void log_call(void *caller, void *callee);

class IndirectCallAnalysis : public ModulePass {
private:
  template <typename T>
  void instrumentCall(Module &M, T *Call, FunctionCallee LogFunc, Function *Func);

public:
  static char ID;
  IndirectCallAnalysis() : ModulePass(ID) {}
  bool runOnModule(Module &M) override;
};

ModulePass *createIndirectCallAnalysisPass();
} // namespace llvm
#endif