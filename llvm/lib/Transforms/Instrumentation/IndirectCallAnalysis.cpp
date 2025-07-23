#include "llvm/Transforms/Instrumentation/IndirectCallAnalysis.h"
#include "llvm/InitializePasses.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/Demangle/Demangle.h"
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Intrinsics.h"

using namespace llvm;
cl::opt<bool> EnableIndirectCallAnalysis(
    "indirect-call-analysis",
    cl::Hidden,
    cl::desc("Enable Indirect Call Analysis Pass"),
    cl::init(false)
);
// void log_call(void* caller, void* callee) {
//     std::cout << "LOG" << std::endl;
//     std::ofstream outFile("IndirectCall.log.txt", std::ios::app);
//     std::streambuf *coutbuf = std::cout.rdbuf();
//     std::cout.rdbuf(outFile.rdbuf());

//     Dl_info caller_info, callee_info;
//     const char* caller_name = "unknown";
//     const char* callee_name = "unknown";

//     if (dladdr(caller, &caller_info) && caller_info.dli_sname)
//         caller_name = caller_info.dli_sname;
//     if (dladdr(callee, &callee_info) && callee_info.dli_sname)
//         callee_name = callee_info.dli_sname;

//     std::string read_caller = llvm::demangle(std::string(caller_name));
//     std::string read_callee = llvm::demangle(std::string(callee_name));

//     std::cout << "[CALL] " << read_caller << "-> " <<  read_callee << std::endl;
//     std::cout.rdbuf(coutbuf);
//     return;
// }

char IndirectCallAnalysis::ID = 0;

bool IndirectCallAnalysis::runOnModule(Module &M) {
    errs() << "[IndirectCall] runOnModule start\n";
    if (!EnableIndirectCallAnalysis)
        return false;
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
                // 跳过内联汇编
                if (isa<InlineAsm>(I)) {
                    continue;
                }
                // 处理 CallInst 和 InvokeInst
                if (auto *CI = dyn_cast<CallInst>(I)) {
                    if (!CI->getCalledFunction()) {
                        instrumentCall(M, CI, LogFunc, &F);
                        Changed = true;
                    }
                } else if (auto *II = dyn_cast<InvokeInst>(I)) {
                    if (!II->getCalledFunction()) {
                        instrumentCall(M, II, LogFunc, &F);
                        Changed = true;
                    }
                }
            }
        }
    }
    return Changed;
}

template <typename T>
void IndirectCallAnalysis::instrumentCall(Module &M, T *Call, FunctionCallee LogFunc, Function *Func) {
    IRBuilder<> Builder(Call);
    Builder.SetInsertPoint(Call);

    auto *RetAddrFunc = Intrinsic::getDeclaration(&M, Intrinsic::returnaddress);
    Value *CallerAddr = Builder.CreateCall(RetAddrFunc, {Builder.getInt32(0)});
    CallerAddr = Builder.CreatePointerCast(CallerAddr, Type::getInt8PtrTy(M.getContext()));

    Value *Callee = Call->getCalledOperand();
    if (isa<InlineAsm>(Callee))
        return;

    Callee = Builder.CreatePointerCast(Callee, Type::getInt8PtrTy(M.getContext()));

    Builder.CreateCall(LogFunc, {CallerAddr, Callee});
}

ModulePass* llvm::createIndirectCallAnalysisPass() { return new IndirectCallAnalysis();}

INITIALIZE_PASS(IndirectCallAnalysis,
                "indirect-call-analysis",
                "Instrument indirect call targets",
                false, false)

static RegisterStandardPasses
  RegisterPassO0(PassManagerBuilder::EP_EnabledOnOptLevel0,
                 [](const PassManagerBuilder &, legacy::PassManagerBase &PM) {
                   PM.add(createIndirectCallAnalysisPass());
                 });

static RegisterStandardPasses
  RegisterPassO1(PassManagerBuilder::EP_OptimizerLast,
                 [](const PassManagerBuilder &, legacy::PassManagerBase &PM) {
                   PM.add(createIndirectCallAnalysisPass());
                 });