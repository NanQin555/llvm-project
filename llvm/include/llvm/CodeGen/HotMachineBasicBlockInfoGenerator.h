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

  SmallVector<SmallVector<std::pair<MachineBasicBlock *, MachineBasicBlock *>>>&
  getSuccNamesCloningInfo(StringRef FuncName);

  // After clone basic block, reorder MBBs to get best performence
  // using EXTTSP algorithm.
  void layoutClonedMBBForFunction(MachineFunction &MF);

private:
  using Edge = std::pair<const MachineBasicBlock *, const MachineBasicBlock *>;
  using BlockWeightMap = DenseMap<const MachineBasicBlock *, uint64_t>;
  using EdgeWeightMap = DenseMap<Edge, uint64_t>;
  using BlockEdgeMap =
    DenseMap<const MachineBasicBlock *, SmallVector<const MachineBasicBlock *, 8>>;

  DenseMap<StringRef, SmallVector<MachineBasicBlock *, 4>> FuncToHotMBBs;
  DenseMap<StringRef, SmallVector<SmallVector<MachineBasicBlock *, 4>>> FuncToMBBClonePaths;
  // Path cloning info: item [0, 4, 2] means in path 0->4->2 needs clone basic block 4 and 2.
  DenseMap<StringRef, SmallVector<SmallVector<unsigned>>> FuncToBBIDClonePaths;
  // Contain cloned MBB for clone paths. If path cloning info is [0, 4, 2], 
  // the SuccBBIDClonePath will be the vector of MBBs whose id is [0, 4, 4.1, 2, 2.1].
  // The base MBB and cloned MBB are saved here.
  DenseMap<StringRef, SmallVector<SmallVector<
    std::pair<MachineBasicBlock * /*Base MBB*/, 
              MachineBasicBlock * /*Cloned MBB*/>>>> FuncToSuccBBIDClonePaths;

  BlockWeightMap MBBToFreq;
  BlockEdgeMap Successors;
  SmallVector<MachineBasicBlock *, 4> HotBBs;

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