#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

namespace {
struct IndirectCallAnalysis : public ModulePass {
    static char ID;
    IndirectCallAnalysis() : ModulePass(ID) {}

    bool runOnModule(Module &M) override {
        bool Changed = false;

        // 声明 log_call 函数原型：void log_call(void* caller, void* callee)
        FunctionType *LogFuncType = FunctionType::get(
            Type::getVoidTy(M.getContext()),
            {Type::getInt8PtrTy(M.getContext()), Type::getInt8PtrTy(M.getContext())},
            false
        );
        FunctionCallee LogFunc = M.getOrInsertFunction("log_call", LogFuncType);

        for (auto &F : M) {
            for (auto &BB : F) {
                for (auto it = BB.begin(); it != BB.end(); ) {
                    Instruction *I = &(*it);
                    ++it;

                    if (CallInst *CI = dyn_cast<CallInst>(I)) {
                        if (!CI->getCalledFunction()) { // Indirect call
                            IRBuilder<> Builder(CI);
                            Value *Caller = &F;
                            Value *Callee = CI->getCalledOperand();

                            // Cast to void*
                            Caller = Builder.CreatePointerCast(Caller, Type::getInt8PtrTy(M.getContext()));
                            Callee = Builder.CreatePointerCast(Callee, Type::getInt8PtrTy(M.getContext()));

                            // Call log_call(caller, callee)
                            Builder.CreateCall(LogFunc, {Caller, Callee});
                            Changed = true;
                        }
                    }
                }
            }
        }

        return Changed;
    }
};
}

char IndirectCallAnalysis::ID = 0;
static RegisterPass<IndirectCallAnalysis> X("indirect-call-analysis",
                                         "Instrument indirect calls to log runtime targets",
                                         false /* Only looks at CFG */,
                                         false /* Analysis Pass */);