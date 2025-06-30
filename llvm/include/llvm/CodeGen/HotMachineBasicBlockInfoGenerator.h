#ifndef LLVM_CODEGEN_HotMachineBasicBlockInfoGenerator_H
#define LLVM_CODEGEN_HotMachineBasicBlockInfoGenerator_H

#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/Transforms/Utils/SampleProfileInference.h"
#include "llvm/CodeGen/FuncHotBBHashesProfileReader.h"
#include <tuple>
namespace llvm {

struct HotMBBInfo {
  MachineBasicBlock *MBB;
  uint64_t Freq;
  uint64_t ClonedId = 0; // default is 0, means not cloned.
  HotMBBInfo(MachineBasicBlock *M, uint64_t F, uint64_t C = 0)
    : MBB(M), Freq(F), ClonedId(C) {}
};

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

  std::optional<SmallVector<HotMBBInfo, 4>> getHotMBBInfos(StringRef FuncName) const;

  std::pair<bool, SmallVector<SmallVector<MachineBasicBlock *, 4>>>
  getMBBPathsCloningInfo(StringRef FuncName) const;

  std::pair<bool, SmallVector<SmallVector<unsigned>>>
  getBBIDPathsCloningInfo(StringRef FuncName) const;

  SmallVector<SmallVector<std::tuple<MachineBasicBlock *, MachineBasicBlock *, unsigned>>>&
  getSuccCloningInfo(StringRef FuncName);

  /** Handle items in FuncToHotMBBInfos. Two main action:
   *  1. Replace origin MBB to cloned MBB for those items 
   *  whose cloned ID is not 0 and cloned successfully.
   *  origin MBB, 12, 3.1 -> cloned MBB, 12, 3.1
   *  2. Remove items that not cloned actually.
   */ 
  void handleHotMBBInfos(MachineFunction &MF, SmallVector<HotMBBInfo, 4> &HotMBBInfos);

  // After clone basic block, reorder MBBs to get best performence
  // using EXTTSP algorithm.
  bool layoutMBBsForFunction(MachineFunction &ME);

  void addToSuccClonePaths(MachineFunction &MF, SmallVector<
    std::tuple<MachineBasicBlock * /*Base MBB*/, 
              MachineBasicBlock * /*Cloned MBB*/,
              unsigned /*Cloned MBB ID*/>> &SuccClonePath);

private:
  using Edge = std::pair<const MachineBasicBlock *, const MachineBasicBlock *>;
  using BlockWeightMap = DenseMap<const MachineBasicBlock *, uint64_t>;
  using EdgeWeightMap = DenseMap<Edge, uint64_t>;
  using BlockEdgeMap =
    DenseMap<const MachineBasicBlock *, SmallVector<const MachineBasicBlock *, 8>>;

  FuncHotBBHashesProfileReader *ProfileReader = nullptr;
  
  DenseMap<StringRef, SmallVector<MachineBasicBlock *, 4>> FuncToHotMBBs;
  DenseMap<StringRef, SmallVector<HotMBBInfo, 4>> FuncToHotMBBInfos;
  
  DenseMap<StringRef, SmallVector<SmallVector<MachineBasicBlock *, 4>>> FuncToMBBClonePaths;

  // Path cloning info: item [0, 4, 2] means in path 0->4->2 needs clone basic block 4 and 2.
  DenseMap<StringRef, SmallVector<SmallVector<unsigned>>> FuncToBBIDClonePaths;

  // Contain cloned MBB for clone paths. If path cloning info is [0, 4, 2], 
  // the SuccBBIDClonePath will be the vector of MBBs whose id is [0, 4, 4.1, 2, 2.1].
  // The base MBB and cloned MBB are saved here.
  DenseMap<StringRef, SmallVector<SmallVector<
    std::tuple<MachineBasicBlock * /*Base MBB*/, 
              MachineBasicBlock * /*Cloned MBB*/,
              unsigned /*Cloned MBB ID*/>>>> FuncToSuccClonePaths;
  
  void matchHotMBBInfosByHashes(
    MachineFunction &MF,
    SmallVector<HotBBInfo, 4> &HotMBBInfos);
  
  void generateHotBBsforFunction(
    MachineFunction &MF,
    BlockWeightMap &OriBlockWeights,
    BlockWeightMap &BlockWeights,
    EdgeWeightMap &EdgeWeights,
    SmallVector<MachineBasicBlock *, 4>  &HotBBs);

  void matchMBBClonePathsByHashes(
    MachineFunction &MF,
    SmallVector<SmallVector<uint64_t, 4>> &HashPathsCloningInfo);   

  void matchBBIDClonePathsByHashes(
    MachineFunction &MF,
    SmallVector<SmallVector<uint64_t, 4>> &HashPathsCloningInfo);
};

} // end namespace llvm

#endif // LLVM_CODEGEN_HotMachineBasicBlockInfoGenerator_H
