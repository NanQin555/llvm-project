#ifndef LLVM_CODEGEN_HotMachineBasicBlockInfoGenerator_H
#define LLVM_CODEGEN_HotMachineBasicBlockInfoGenerator_H

#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/Transforms/Utils/SampleProfileInference.h"
#include "llvm/CodeGen/FuncHotBBHashesProfileReader.h"

namespace llvm {

class HotMachineBasicBlockInfoGenerator : public MachineFunctionPass {
public:
  static char ID;
  HotMachineBasicBlockInfoGenerator();

  StringRef getPassName() const override {
    return "Basic Block Matching and Inference";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  bool runOnMachineFunction(MachineFunction &F) override;

  std::optional<SmallVector<MachineBasicBlock *, 4>> getHotMBBs(StringRef FuncName) const;

  std::pair<bool, SmallVector<SmallVector<MachineBasicBlock *, 4>>>
  getMBBPathsCloningInfo(StringRef FuncName) const;

  std::pair<bool, SmallVector<SmallVector<unsigned>>>
  getBBIDPathsCloningInfo(StringRef FuncName) const;

private:
  using Edge = std::pair<const MachineBasicBlock *, const MachineBasicBlock *>;
  using BlockWeightMap = DenseMap<const MachineBasicBlock *, uint64_t>;
  using EdgeWeightMap = DenseMap<Edge, uint64_t>;
  using BlockEdgeMap =
    DenseMap<const MachineBasicBlock *, SmallVector<const MachineBasicBlock *, 8>>;

  DenseMap<StringRef, SmallVector<MachineBasicBlock *, 4>> FuncToHotMBBs;
  DenseMap<StringRef, SmallVector<SmallVector<MachineBasicBlock *, 4>>> FuncToMBBClonePaths;
  DenseMap<StringRef, SmallVector<SmallVector<unsigned>>> FuncToBBIDClonePaths;

  void matchHotBBsByHashes(
    MachineFunction &MF,
    SmallVector<HotBBInfo, 4> &HotMBBInfos,
    BlockWeightMap &MBBToFreq, 
    BlockEdgeMap &Successors,
    SmallVector<MachineBasicBlock *, 4> &HotBBs);
  
  void generateHotBBsforFunction(
    MachineFunction &MF,
    BlockWeightMap &OriBlockWeights,
    BlockWeightMap &BlockWeights,
    EdgeWeightMap &EdgeWeights,
    SmallVector<MachineBasicBlock *, 4> &HotBBs);

  void matchMBBClonePathsByHashes(
    MachineFunction &MF,
    SmallVector<SmallVector<uint64_t, 4>> &HashPathsCloningInfo);   

  void matchBBIDClonePathsByHashes(
    MachineFunction &MF,
    SmallVector<SmallVector<uint64_t, 4>> &HashPathsCloningInfo);
};

} // end namespace llvm

#endif // LLVM_CODEGEN_HotMachineBasicBlockInfoGenerator_H